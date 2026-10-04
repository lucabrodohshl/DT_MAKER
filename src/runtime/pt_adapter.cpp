/**
 * @file pt_adapter.cpp
 * @brief Label translation via the verified label-equivalence relation E.
 */
#include "twin/runtime/pt_adapter.hpp"

namespace twin::runtime {

Result<PtAdapter> PtAdapter::from_evidence(const json::Json& evidence) {
    if (!evidence.contains("label_equivalence") || !evidence.at("label_equivalence").is_array()) {
        return make_error(ErrorCode::ValidationError, "alignment evidence has no label_equivalence");
    }
    PtAdapter a;
    for (const json::Json& row : evidence.at("label_equivalence")) {
        const std::string pt = row.value("pt", std::string());
        const json::Json dt = row.value("dt", json::Json::array());
        if (!pt.empty() && dt.is_array() && dt.size() == 1 && dt[0].is_string()) {
            a.table_[pt] = dt[0].get<std::string>();
        }
    }
    if (a.table_.empty()) {
        return make_error(ErrorCode::ValidationError, "label_equivalence maps no PT label to a unique DT label");
    }
    return a;
}

Translation PtAdapter::translate(const json::Json& pt_event) const {
    Translation t;
    t.pt_label = pt_event.value("label", std::string());
    const auto it = table_.find(t.pt_label);
    if (it == table_.end()) {
        t.note = "PT event '" + t.pt_label + "' has no unique DT equivalent in E (internal to the PT)";
        return t;
    }
    ledger::Input in;
    in.source = "pt-adapter";
    in.kind = ledger::InputKind::Label;
    in.name = it->second;
    in.at = pt_event.value("at", Ticks{0});
    in.payload = json::Json{{"pt_label", t.pt_label}, {"detail", pt_event.value("detail", json::Json::object())}};
    t.input = std::move(in);
    return t;
}

}  // namespace twin::runtime
