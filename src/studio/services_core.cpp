/**
 * @file services_core.cpp
 * @brief Services: construction, shared helpers, registry upserts, bindings (see services.hpp).
 */
#include <fstream>

#include "services_impl.hpp"
#include "twin/alignment/aligner_identity.hpp"
#include "twin/core/version.hpp"
#include "twin/ontology/verdict.hpp"

namespace twin::studio {

namespace fs = std::filesystem;
using namespace twin::platform;

// --- Impl helpers -------------------------------------------------------------------

void Services::Impl::record(std::string_view operation, std::string_view outcome, std::string_view subject,
                            json::Json details, const Actor& actor, std::string_view topic) {
    auto r = audit->append(operation, outcome, subject, details, actor.name);
    if (!r) {
        self.log_->write(LogLevel::Error, "studio.audit", "failed to append audit record",
                         {{"operation", std::string(operation)}, {"error", r.error().to_string()}});
        return;
    }
    self.events_.publish(std::string(topic),
                         {{"operation", std::string(operation)},
                          {"subject", std::string(subject)},
                          {"outcome", std::string(outcome)},
                          {"auditSeq", r.value().seq}},
                         r.value().at);
}

Result<fs::path> Services::Impl::materialize(const Binding& b) {
    std::string ext = ".txt";
    if (b.role == "pt_model" || b.role == "dt_model") ext = ".xml";
    else if (b.role == "ontology") ext = ".ont";
    else if (b.role == "pt_interpretation" || b.role == "dt_interpretation") ext = ".interp";
    const fs::path dir = self.config_.data_dir / "work";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot create work directory: " + ec.message());
    const fs::path path = dir / (b.sha256 + ext);
    if (fs::exists(path, ec)) return path;
    auto bytes = store->get(b.sha256);
    if (!bytes) return std::move(bytes).error();
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << bytes.value();
        if (!out) return make_error(ErrorCode::IoError, "cannot write work file");
    }
    fs::rename(tmp, path, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot finalise work file: " + ec.message());
    return path;
}

Result<Binding> Services::Impl::bind(std::string role, const ArtifactRef& ref) {
    auto v = artifacts->version(ref);
    if (!v) return std::move(v).error();
    return Binding{std::move(role), ref, v.value().content_sha256};
}

Result<json::Json> Services::Impl::version_summary(const ArtifactRef& ref) {
    auto v = artifacts->version(ref);
    if (!v) return std::move(v).error();
    auto a = artifacts->get(ref.artifact_id);
    if (!a) return std::move(a).error();
    json::Json j = to_json(v.value());
    j["name"] = a.value().name;
    return j;
}

bool Services::Impl::is_running(const std::string& key) {
    const std::lock_guard<std::mutex> l(running_mu);
    return running.count(key) != 0;
}

json::Json Services::Impl::evidence_json(const std::vector<EvidenceRecord>& records) {
    json::Json out = json::Json::array();
    for (const auto& r : records) out.push_back(to_json(r));
    return out;
}

RunningCheck::RunningCheck(Services::Impl& impl, std::string key) : impl_(impl), key_(std::move(key)) {
    const std::lock_guard<std::mutex> l(impl_.running_mu);
    acquired_ = impl_.running.insert(key_).second;
}

RunningCheck::~RunningCheck() {
    if (!acquired_) return;
    const std::lock_guard<std::mutex> l(impl_.running_mu);
    impl_.running.erase(key_);
}

const Binding* find_binding(const std::vector<Binding>& bindings, std::string_view role) {
    for (const auto& b : bindings) {
        if (b.role == role) return &b;
    }
    return nullptr;
}

// --- construction -------------------------------------------------------------------

Services::Services(StudioConfig config, std::unique_ptr<Clock> clock)
    : config_(std::move(config)),
      clock_(std::move(clock)),
      log_(std::make_unique<AppLog>(config_.data_dir / "logs" / "studio.jsonl", *clock_)),
      impl_(std::make_unique<Impl>(*this)) {}

Services::~Services() { events_.close(); }

Result<std::unique_ptr<Services>> Services::open(const StudioConfig& config, std::unique_ptr<Clock> clock) {
    std::error_code ec;
    fs::create_directories(config.data_dir, ec);
    if (ec) return make_error(ErrorCode::IoError, "cannot create data directory: " + ec.message());
    if (!clock) clock = std::make_unique<SystemClock>();
    std::unique_ptr<Services> s(new Services(config, std::move(clock)));
    auto db = Database::open((config.data_dir / "studio.db").string());
    if (!db) return std::move(db).error();
    auto store = ObjectStore::open(config.data_dir);
    if (!store) return std::move(store).error();
    Impl& i = *s->impl_;
    i.db = std::move(db).value();
    i.store = std::make_unique<ObjectStore>(std::move(store).value());
    i.artifacts = std::make_unique<ArtifactRepository>(*i.db, *i.store, *s->clock_);
    i.evidence = std::make_unique<EvidenceRepository>(*i.db, *i.store, *s->clock_);
    i.audit = std::make_unique<AuditLog>(*i.db, *s->clock_);
    i.assets = std::make_unique<AssetRepository>(*i.db);
    i.telemetry = std::make_unique<TelemetryRepository>(*i.db);
    i.twins = std::make_unique<TwinRepository>(*i.db, *s->clock_);
    s->log_->write(LogLevel::Info, "studio.services", "data directory opened",
                   {{"dataDir", config.data_dir.string()}, {"schemaVersion", i.db->schema_version()}});
    return s;
}

json::Json Services::about() const {
    return {{"product", "Verified Twin Studio"},
            {"version", std::string(version::kProject)},
            {"compilerVersion", std::string(version::kCompiler)},
            {"kernelVersion", std::string(version::kKernel)},
            {"kernelCompat", std::string(version::kKernelCompat)},
            {"irFormat", std::string(version::kIrFormat)},
            {"packageFormat", std::string(version::kPackageFormat)},
            {"ledgerSchema", std::string(version::kLedgerSchema)},
            {"aligner", std::string(alignment::kAlignerName)},
            {"alignerDigest", std::string(alignment::kAlignerSourceDigest)},
            {"ontologyServices", std::string(ontology::kOntologyServicesVersion)},
            {"ontologyChecker", ontology::checker_identity()},
            {"epoch", events_.epoch()}};
}

// --- registry ----------------------------------------------------------------------

Result<Twin> Services::twin_record(std::string_view id) {
    auto l = impl_->lock();
    return impl_->twins->twin(id);
}

Status Services::upsert_asset(const Asset& asset) {
    auto l = impl_->lock();
    return impl_->assets->upsert(asset);
}

Status Services::relate(std::string_view source, std::string_view type, std::string_view target) {
    auto l = impl_->lock();
    auto r = impl_->assets->relate(source, type, target);
    if (!r) return std::move(r).error();
    return {};
}

Status Services::upsert_twin(const Twin& twin) {
    auto l = impl_->lock();
    return impl_->twins->upsert_twin(twin);
}

Status Services::upsert_channel(const TelemetryChannel& channel) {
    auto l = impl_->lock();
    return impl_->telemetry->upsert_channel(channel);
}

Result<std::int64_t> Services::ingest_samples(std::string_view channel_id, const std::vector<TelemetrySample>& samples) {
    Result<std::int64_t> n = std::int64_t{0};
    {
        auto l = impl_->lock();
        n = impl_->telemetry->ingest(channel_id, samples);
    }
    if (n && !samples.empty()) {
        events_.publish("telemetry", {{"channelId", std::string(channel_id)}, {"count", n.value()},
                                      {"lastObservedMs", samples.back().observed_ms}},
                        iso8601_utc(clock_->now_ms()));
    }
    return n;
}

// --- bindings -----------------------------------------------------------------------

Result<std::vector<Binding>> Services::deployed_bindings(std::string_view twin_id) {
    auto l = impl_->lock();
    auto dep = impl_->twins->current_deployment(twin_id);
    if (!dep) return std::move(dep).error();
    if (!dep.value()) return std::vector<Binding>{};
    auto pkg = impl_->twins->package(dep.value()->package_id);
    if (!pkg) return std::move(pkg).error();
    return pkg.value().bindings;
}

Result<std::vector<Binding>> Services::candidate_bindings(std::string_view change_id) {
    auto l = impl_->lock();
    auto c = impl_->twins->change(change_id);
    if (!c) return std::move(c).error();
    auto base = deployed_bindings(c.value().twin_id);
    if (!base) return std::move(base).error();
    std::vector<Binding> out = std::move(base).value();
    for (const auto& ref : c.value().artifacts) {
        auto v = impl_->artifacts->version(ref);
        if (!v) return std::move(v).error();
        bool replaced = false;
        for (auto& b : out) {
            if (b.ref.artifact_id == ref.artifact_id) {
                b.ref = ref;
                b.sha256 = v.value().content_sha256;
                replaced = true;
            }
        }
        if (!replaced) {
            return make_error(ErrorCode::ValidationError,
                              "change artefact is not bound by the twin's deployment; it cannot be released with it")
                .with("artifact", ref.str());
        }
    }
    return out;
}

Result<std::vector<Binding>> Services::make_bindings(const std::vector<std::pair<std::string, ArtifactRef>>& roles) {
    auto l = impl_->lock();
    std::vector<Binding> out;
    for (const auto& [role, ref] : roles) {
        if (std::find(kBindingRoles.begin(), kBindingRoles.end(), role) == kBindingRoles.end()) {
            return make_error(ErrorCode::InvalidArgument, "unknown binding role").with("role", role);
        }
        auto b = impl_->bind(role, ref);
        if (!b) return std::move(b).error();
        out.push_back(std::move(b).value());
    }
    return out;
}

}  // namespace twin::studio
