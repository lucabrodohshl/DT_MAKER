/**
 * @file launcher.mm
 * @brief macOS launcher of Verified Twin Studio (the .app's main executable).
 *
 * Double-clicking the app must simply work, whatever else runs on this Mac. The
 * launcher therefore:
 *  1. makes sure it is the only launcher using its data directory
 *     (~/Library/Application Support/Verified Twin Studio): if another one is
 *     running it opens that one and quits; processes left over by a launcher that
 *     crashed are stopped (only this app's own twin-* executables, never others);
 *  2. moves data written by an incompatible earlier version aside (kept as a
 *     backup folder) and seeds the examples on first start — with real
 *     validation, alignment, compilation and packaging;
 *  3. picks free ports (the defaults 8080/8090/8091/8092 when they are free,
 *     otherwise nearby free ones), so it coexists with a developer `make demo`
 *     or any other server;
 *  4. starts twin-world, twin-studio, the two twin runtimes (each on exactly the
 *     package Studio deployed for its twin) and the pump's PLC feed, waits until
 *     each answers, and opens the product in the default browser;
 *  5. supervises every component: one that stops unexpectedly is restarted with
 *     back-off; if it keeps failing, everything is stopped and a dialog offers
 *     the logs, a data reset or quitting — never a half-running backend.
 * A menu-bar item offers Open, the data/log folders, Reset demo data and Quit;
 * quitting stops every component. Logs go to ~/Library/Logs/Verified Twin Studio/.
 *
 * Environment (tests): VTS_DATA_DIR (isolated data directory), VTS_NO_BROWSER,
 * VTS_STUDIO_PORT/VTS_DRONE_PORT/VTS_WORLD_PORT/VTS_PUMP_PORT (preferred ports).
 *
 * The launcher contains no product logic: it is the macOS equivalent of
 * scripts/start-demo.sh (which remains the reference for developers).
 */
#import <Cocoa/Cocoa.h>

#include <arpa/inet.h>
#include <libproc.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

/// Data layout version; data written by an older, incompatible version is moved aside.
static NSString* const kDataFormat = @"vts-data/2";

/// Preferred port from the environment (VTS_<NAME>_PORT) or the default.
static int PortSetting(const char* env, int fallback) {
    const char* v = getenv(env);
    const int p = v ? atoi(v) : 0;
    return p > 0 && p < 65536 ? p : fallback;
}

/// True if a server can bind 127.0.0.1:port right now.
static BOOL PortFree(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NO;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const BOOL ok = bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr) == 0;
    close(fd);
    return ok;
}

/// @p preferred if free, else the first free port after it (skipping @p taken); 0 if none.
static int ChoosePort(int preferred, NSMutableSet<NSNumber*>* taken) {
    for (int p = preferred; p < preferred + 2000 && p < 65536; ++p) {
        if (p != preferred && p < 1024) continue;
        if ([taken containsObject:@(p)]) continue;
        if (PortFree(p)) {
            [taken addObject:@(p)];
            return p;
        }
    }
    return 0;
}

/// Executable path of a process, or nil.
static NSString* ProcessPath(pid_t pid) {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (pid <= 0 || proc_pidpath(pid, path, sizeof path) <= 0) return nil;
    return [NSString stringWithUTF8String:path];
}

/// Synchronous HTTP GET returning parsed JSON (nil on failure).
static id GetJson(NSString* url, NSTimeInterval timeout) {
    __block id result = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url]];
    req.timeoutInterval = timeout;
    [[[NSURLSession sharedSession] dataTaskWithRequest:req
                                     completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                                         NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
                                         if (!error && data && http.statusCode == 200) {
                                             result = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
                                         }
                                         dispatch_semaphore_signal(done);
                                     }] resume];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)((timeout + 1) * NSEC_PER_SEC)));
    return result;
}

static BOOL WaitForJson(NSString* url, NSTimeInterval seconds) {
    for (int i = 0; i < seconds * 4; ++i) {
        if (GetJson(url, 1.0) != nil) return YES;
        [NSThread sleepForTimeInterval:0.25];
    }
    return NO;
}

/// One supervised component of the stack.
@interface StackComponent : NSObject
@property(copy) NSString* name;          ///< Log name, e.g. "twin-runtime-pump".
@property(copy) NSString* executable;    ///< Executable in Contents/Resources/bin.
@property(copy) NSArray<NSString*>* args;
@property(copy) NSString* healthURL;     ///< Answers 200 with JSON when ready (nil: none).
@property(strong) NSTask* task;
@property(strong) NSMutableArray<NSDate*>* restarts;
@end
@implementation StackComponent
@end

@interface StudioLauncher : NSObject <NSApplicationDelegate>
@property(strong) NSStatusItem* statusItem;
@property(strong) NSMenuItem* statusLine;
@property(strong) NSPanel* progressPanel;
@property(strong) NSTextField* progressText;
@property(strong) NSMutableArray<StackComponent*>* components;
@property(copy) NSString* resources;
@property(copy) NSString* dataDir;
@property(copy) NSString* logDir;
@property(copy) NSString* studioURL;
@property BOOL stopping;
@property BOOL failed;
@end

@implementation StudioLauncher

- (NSString*)bin:(NSString*)name {
    return [[self.resources stringByAppendingPathComponent:@"bin"] stringByAppendingPathComponent:name];
}

- (NSString*)res:(NSString*)relative {
    return [self.resources stringByAppendingPathComponent:relative];
}

- (NSString*)statePath {
    return [[self.dataDir stringByAppendingPathComponent:@"run"] stringByAppendingPathComponent:@"launcher.json"];
}

- (void)setStatus:(NSString*)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLine.title = text;
        self.progressText.stringValue = text;
    });
}

// ------------------------------------------------------------------ progress window

- (void)showProgress {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSPanel* p = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 420, 120)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
        p.title = @"Verified Twin Studio";
        p.releasedWhenClosed = NO;
        NSTextField* text = [NSTextField labelWithString:@"Starting…"];
        text.frame = NSMakeRect(20, 64, 380, 36);
        text.lineBreakMode = NSLineBreakByWordWrapping;
        text.maximumNumberOfLines = 2;
        NSProgressIndicator* spinner = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 28, 380, 20)];
        spinner.style = NSProgressIndicatorStyleBar;
        spinner.indeterminate = YES;
        [spinner startAnimation:nil];
        [p.contentView addSubview:text];
        [p.contentView addSubview:spinner];
        [p center];
        [p makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        self.progressPanel = p;
        self.progressText = text;
    });
}

- (void)hideProgress {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.progressPanel orderOut:nil];
    });
}

// ------------------------------------------------------------------ processes

/// Start @p c; stdout/stderr go to <logDir>/<name>.log (appended).
- (BOOL)launch:(StackComponent*)c {
    NSString* logPath = [self.logDir stringByAppendingPathComponent:[c.name stringByAppendingString:@".log"]];
    if (![[NSFileManager defaultManager] fileExistsAtPath:logPath]) {
        [[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
    }
    NSFileHandle* out = [NSFileHandle fileHandleForWritingAtPath:logPath];
    [out seekToEndOfFile];
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:[self bin:c.executable]];
    task.arguments = c.args;
    task.currentDirectoryURL = [NSURL fileURLWithPath:self.resources];
    task.standardOutput = out;
    task.standardError = out;
    __weak StudioLauncher* weakSelf = self;
    __weak StackComponent* weakC = c;
    task.terminationHandler = ^(NSTask* t) {
        StudioLauncher* s = weakSelf;
        StackComponent* comp = weakC;
        if (s && comp && !s.stopping && !s.failed && comp.task == t) [s componentStopped:comp status:t.terminationStatus];
    };
    NSError* error = nil;
    if (![task launchAndReturnError:&error]) return NO;
    c.task = task;
    [self writeState];
    return YES;
}

/// A component stopped unexpectedly: restart it with back-off, or give up after repeated failures.
- (void)componentStopped:(StackComponent*)c status:(int)status {
    NSDate* now = [NSDate date];
    [c.restarts filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSDate* d, NSDictionary* b) {
                    return [now timeIntervalSinceDate:d] < 120;
                }]];
    if (c.restarts.count >= 5) {
        [self fail:[NSString stringWithFormat:@"%@ stopped repeatedly (last exit status %d).", c.name, status]];
        return;
    }
    [c.restarts addObject:now];
    const double delay = MIN(8.0, 0.5 * (1 << c.restarts.count));
    [self setStatus:[NSString stringWithFormat:@"Restarting %@…", c.name]];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                       if (self.stopping || self.failed) return;
                       if (![self launch:c]) {
                           [self componentStopped:c status:-1];
                           return;
                       }
                       if (c.healthURL && !WaitForJson(c.healthURL, 30)) return;  // its own termination handler retries
                       [self setStatus:[NSString stringWithFormat:@"Running at %@", self.studioURL]];
                   });
}

/// Run a component to completion (seeding); returns its exit status.
- (int)runToCompletion:(NSString*)executable args:(NSArray<NSString*>*)args log:(NSString*)log {
    StackComponent* c = [[StackComponent alloc] init];
    c.name = log;
    c.executable = executable;
    c.args = args;
    self.stopping = YES;  // a seeding run ending is not a failure
    if (![self launch:c]) {
        self.stopping = NO;
        return -1;
    }
    [c.task waitUntilExit];
    self.stopping = NO;
    return c.task.terminationStatus;
}

- (void)writeState {
    NSMutableArray* children = [NSMutableArray array];
    for (StackComponent* c in self.components) {
        if (c.task.running) [children addObject:@{@"name" : c.name, @"pid" : @(c.task.processIdentifier)}];
    }
    NSDictionary* state = @{
        @"launcherPid" : @(getpid()),
        @"studioURL" : self.studioURL ?: @"",
        @"children" : children,
    };
    NSData* data = [NSJSONSerialization dataWithJSONObject:state options:NSJSONWritingPrettyPrinted error:nil];
    [[NSFileManager defaultManager] createDirectoryAtPath:[self.statePath stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];
    [data writeToFile:self.statePath atomically:YES];
}

/**
 * If another launcher owns the data directory, return its Studio URL. Otherwise stop
 * the twin-* processes a crashed launcher left behind (only executables named twin-*
 * inside a Verified Twin Studio bundle) and return nil.
 */
- (NSString*)claimDataDirectory {
    NSData* data = [NSData dataWithContentsOfFile:self.statePath];
    NSDictionary* state = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if (![state isKindOfClass:[NSDictionary class]]) return nil;
    const pid_t other = [state[@"launcherPid"] intValue];
    NSString* otherPath = ProcessPath(other);
    if (other != getpid() && [otherPath hasSuffix:@"/Contents/MacOS/VerifiedTwinStudio"]) {
        // Another copy of the app owns this data (it may still be starting): use it, never touch its components.
        [self setStatus:@"Verified Twin Studio is already running; connecting…"];
        for (int i = 0; i < 180; ++i) {
            NSData* again = [NSData dataWithContentsOfFile:self.statePath];
            NSDictionary* s = again ? [NSJSONSerialization JSONObjectWithData:again options:0 error:nil] : nil;
            NSString* url = [s isKindOfClass:[NSDictionary class]] ? s[@"studioURL"] : nil;
            if ([url isKindOfClass:[NSString class]] && url.length > 0 &&
                GetJson([url stringByAppendingString:@"/api/v1/twins"], 2) != nil) {
                return url;
            }
            if (ProcessPath(other) == nil) break;  // it quit meanwhile: take over below
            [NSThread sleepForTimeInterval:0.5];
        }
        if (ProcessPath(other) != nil) return @"";  // still alive but not answering: leave it alone
    }
    for (NSDictionary* child in state[@"children"]) {
        const pid_t pid = [child[@"pid"] intValue];
        NSString* path = ProcessPath(pid);
        if (path && [path containsString:@".app/Contents/Resources/bin/twin-"]) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20 && ProcessPath(pid); ++i) [NSThread sleepForTimeInterval:0.1];
            if (ProcessPath(pid)) kill(pid, SIGKILL);
        }
    }
    [[NSFileManager defaultManager] removeItemAtPath:self.statePath error:nil];
    return nil;
}

/// Move data written by an incompatible version aside (kept as a backup) so that seeding starts clean.
- (void)checkDataFormat {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* formatFile = [self.dataDir stringByAppendingPathComponent:@"FORMAT"];
    NSString* format = [NSString stringWithContentsOfFile:formatFile encoding:NSUTF8StringEncoding error:nil];
    format = [format stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    const BOOL hasData = [fm fileExistsAtPath:[self.dataDir stringByAppendingPathComponent:@"studio"]];
    if (!hasData || [format isEqualToString:kDataFormat]) return;
    NSDateFormatter* f = [[NSDateFormatter alloc] init];
    f.dateFormat = @"yyyy-MM-dd HHmmss";
    NSString* backup = [NSString stringWithFormat:@"%@ (backup %@)", self.dataDir, [f stringFromDate:[NSDate date]]];
    [self setStatus:@"Moving data of an earlier version aside…"];
    if (![fm moveItemAtPath:self.dataDir toPath:backup error:nil]) {
        [fm removeItemAtPath:[self.dataDir stringByAppendingPathComponent:@"studio"] error:nil];
    }
    [fm createDirectoryAtPath:self.dataDir withIntermediateDirectories:YES attributes:nil error:nil];
}

- (NSString*)deployedPackageDir:(NSString*)twin {
    NSDictionary* t = GetJson([NSString stringWithFormat:@"%@/api/v1/twins/%@", self.studioURL, twin], 5.0);
    NSString* pkg = nil;
    if ([t isKindOfClass:[NSDictionary class]]) {
        id dep = t[@"deployment"];
        if ([dep isKindOfClass:[NSDictionary class]]) pkg = dep[@"packageId"];
        if (!pkg && [t[@"package"] isKindOfClass:[NSDictionary class]]) pkg = t[@"package"][@"id"];
    }
    if (![pkg isKindOfClass:[NSString class]]) return nil;
    return [[self.dataDir stringByAppendingPathComponent:@"studio/packages"] stringByAppendingPathComponent:pkg];
}

- (void)fail:(NSString*)message {
    if (self.failed) return;
    self.failed = YES;
    [self setStatus:@"⚠︎ Not running — see logs"];
    [self hideProgress];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self stopStack];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Verified Twin Studio could not start";
            alert.informativeText = [message stringByAppendingFormat:@"\n\nLogs: %@", self.logDir];
            [alert addButtonWithTitle:@"Reset Demo Data and Retry"];
            [alert addButtonWithTitle:@"Show Logs"];
            [alert addButtonWithTitle:@"Quit"];
            [NSApp activateIgnoringOtherApps:YES];
            const NSModalResponse r = [alert runModal];
            if (r == NSAlertFirstButtonReturn) {
                [self resetAndRestart];
            } else {
                if (r == NSAlertSecondButtonReturn) [self showLogs:nil];
                [NSApp terminate:nil];
            }
        });
    });
}

/// StackComponent @p name started with @p args.
- (StackComponent*)component:(NSString*)name exe:(NSString*)exe args:(NSArray<NSString*>*)args health:(NSString*)health {
    StackComponent* c = [[StackComponent alloc] init];
    c.name = name;
    c.executable = exe;
    c.args = args;
    c.healthURL = health;
    c.restarts = [NSMutableArray array];
    [self.components addObject:c];
    return c;
}

- (BOOL)launchAndWait:(StackComponent*)c seconds:(NSTimeInterval)seconds {
    if (![self launch:c]) return NO;
    return c.healthURL == nil || WaitForJson(c.healthURL, seconds);
}

- (void)startStack {
    self.failed = NO;
    NSFileManager* fm = [NSFileManager defaultManager];
    [self showProgress];
    NSString* existing = [self claimDataDirectory];
    if (existing.length == 0 && existing != nil) {
        [self hideProgress];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Verified Twin Studio is already running";
            alert.informativeText = @"Another copy is still starting or does not respond. Use its menu-bar item, "
                                    @"or quit it and open the app again.";
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            [NSApp terminate:nil];
        });
        return;
    }
    if (existing) {
        self.studioURL = existing;
        [self setStatus:@"Already running"];
        [self hideProgress];
        [self openStudio:nil];
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp terminate:nil];
        });
        return;
    }
    [self checkDataFormat];

    NSMutableSet<NSNumber*>* taken = [NSMutableSet set];
    const int studioPort = ChoosePort(PortSetting("VTS_STUDIO_PORT", 8080), taken);
    const int dronePort = ChoosePort(PortSetting("VTS_DRONE_PORT", 8090), taken);
    const int worldPort = ChoosePort(PortSetting("VTS_WORLD_PORT", 8091), taken);
    const int pumpPort = ChoosePort(PortSetting("VTS_PUMP_PORT", 8092), taken);
    if (studioPort == 0 || dronePort == 0 || worldPort == 0 || pumpPort == 0) {
        [self fail:@"No free network port was found on this Mac."];
        return;
    }
    NSString* (^url)(int) = ^NSString*(int port) { return [NSString stringWithFormat:@"http://127.0.0.1:%d", port]; };
    self.studioURL = url(studioPort);

    NSString* studioData = [self.dataDir stringByAppendingPathComponent:@"studio"];
    [fm createDirectoryAtPath:[self.dataDir stringByAppendingPathComponent:@"ledgers/drone"] withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtPath:[self.dataDir stringByAppendingPathComponent:@"ledgers/pump"] withIntermediateDirectories:YES attributes:nil error:nil];
    if (![fm fileExistsAtPath:[studioData stringByAppendingPathComponent:@"studio.db"]]) {
        [self setStatus:@"First start: verifying, compiling and packaging the example twins (about a minute)…"];
        for (NSString* example in @[ @"examples/industrial-pump", @"examples/indoor-drone" ]) {
            const int rc = [self runToCompletion:@"twin-studio"
                                            args:@[ @"seed", @"--example", [self res:example], @"--data-dir", studioData ]
                                             log:@"seed"];
            if (rc != 0) {
                [fm removeItemAtPath:studioData error:nil];
                [self fail:[NSString stringWithFormat:@"Preparing the example %@ failed (exit %d).", example.lastPathComponent, rc]];
                return;
            }
        }
        [kDataFormat writeToFile:[self.dataDir stringByAppendingPathComponent:@"FORMAT"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }

    [self setStatus:@"Starting the backend…"];
    StackComponent* world = [self component:@"twin-world" exe:@"twin-world"
                                  args:@[ @"--scenario", [self res:@"scenarios/inspection_default.json"], @"--port", @(worldPort).stringValue ]
                                health:[url(worldPort) stringByAppendingString:@"/health"]];
    StackComponent* studio = [self component:@"twin-studio" exe:@"twin-studio"
                                   args:@[ @"serve", @"--data-dir", studioData, @"--port", @(studioPort).stringValue,
                                           @"--web-root", [self res:@"web"],
                                           @"--runtime", [@"indoor-drone-dt=" stringByAppendingString:url(dronePort)],
                                           @"--world", [@"indoor-drone-dt=" stringByAppendingString:url(worldPort)],
                                           @"--runtime", [@"pump-p101-dt=" stringByAppendingString:url(pumpPort)] ]
                                 health:[self.studioURL stringByAppendingString:@"/api/v1/twins"]];
    if (![self launchAndWait:world seconds:30]) {
        [self fail:@"The building simulator (twin-world) did not start."];
        return;
    }
    if (![self launchAndWait:studio seconds:90]) {
        [self fail:@"The Studio server did not start."];
        return;
    }
    NSString* dronePkg = [self deployedPackageDir:@"indoor-drone-dt"];
    NSString* pumpPkg = [self deployedPackageDir:@"pump-p101-dt"];
    if (!dronePkg || !pumpPkg) {
        [self fail:@"A twin has no deployed package."];
        return;
    }
    NSString* store = [studioData stringByAppendingPathComponent:@"packages"];
    StackComponent* drone = [self component:@"twin-runtime-drone" exe:@"twin-runtime"
                                  args:@[ @"--package", dronePkg, @"--world", url(worldPort), @"--port", @(dronePort).stringValue,
                                          @"--ledger-dir", [self.dataDir stringByAppendingPathComponent:@"ledgers/drone"],
                                          @"--package-store", store, @"--speed", @"1.5", @"--paused" ]
                                health:[url(dronePort) stringByAppendingString:@"/health"]];
    StackComponent* pump = [self component:@"twin-runtime-pump" exe:@"twin-runtime"
                                 args:@[ @"--package", pumpPkg, @"--monitor", @"--port", @(pumpPort).stringValue,
                                         @"--ledger-dir", [self.dataDir stringByAppendingPathComponent:@"ledgers/pump"],
                                         @"--package-store", store ]
                               health:[url(pumpPort) stringByAppendingString:@"/health"]];
    if (![self launchAndWait:drone seconds:30] || ![self launchAndWait:pump seconds:30]) {
        [self fail:@"A twin runtime did not start."];
        return;
    }
    StackComponent* feed = [self component:@"twin-pt-feed" exe:@"twin-pt-feed"
                                 args:@[ @"--feed", [self res:@"scenarios/pump_operating_cycle.json"], @"--runtime", url(pumpPort), @"--speed", @"1" ]
                               health:nil];
    if (![self launch:feed]) {
        [self fail:@"The pump's control-system feed did not start."];
        return;
    }
    [self setStatus:[NSString stringWithFormat:@"Running at %@", self.studioURL]];
    [self hideProgress];
    [self openStudio:nil];
}

- (void)stopStack {
    self.stopping = YES;
    NSArray<StackComponent*>* reversed = [[self.components reverseObjectEnumerator] allObjects];
    for (StackComponent* c in reversed) {
        if (c.task.running) [c.task terminate];
    }
    for (StackComponent* c in reversed) {
        for (int i = 0; i < 30 && c.task.running; ++i) [NSThread sleepForTimeInterval:0.1];
        if (c.task.running) kill(c.task.processIdentifier, SIGKILL);
    }
    [self.components removeAllObjects];
    [[NSFileManager defaultManager] removeItemAtPath:self.statePath error:nil];
    self.stopping = NO;
}

- (void)resetAndRestart {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self stopStack];
        [[NSFileManager defaultManager] removeItemAtPath:self.dataDir error:nil];
        [[NSFileManager defaultManager] createDirectoryAtPath:self.dataDir withIntermediateDirectories:YES attributes:nil error:nil];
        [self startStack];
    });
}

// ------------------------------------------------------------------ menu actions

- (void)openStudio:(id)sender {
    if (getenv("VTS_NO_BROWSER") != nullptr || self.studioURL == nil) return;  // tests
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:self.studioURL]];
    });
}

- (void)showData:(id)sender {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:self.dataDir]];
}

- (void)showLogs:(id)sender {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:self.logDir]];
}

- (void)resetData:(id)sender {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Reset the demo data?";
    alert.informativeText = @"This deletes the Studio database, packages and execution ledgers of this Mac's demo, then prepares the examples again.";
    [alert addButtonWithTitle:@"Reset"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn) return;
    [self resetAndRestart];
}

// ------------------------------------------------------------------ lifecycle

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    self.components = [NSMutableArray array];
    self.resources = [[NSBundle mainBundle] resourcePath];
    NSString* support = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    NSString* library = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES).firstObject;
    const char* dataOverride = getenv("VTS_DATA_DIR");  // tests: an isolated data directory
    self.dataDir = dataOverride ? [NSString stringWithUTF8String:dataOverride]
                                : [support stringByAppendingPathComponent:@"Verified Twin Studio"];
    self.logDir = dataOverride ? [[NSString stringWithUTF8String:dataOverride] stringByAppendingPathComponent:@"logs"]
                               : [[library stringByAppendingPathComponent:@"Logs"] stringByAppendingPathComponent:@"Verified Twin Studio"];
    [[NSFileManager defaultManager] createDirectoryAtPath:self.dataDir withIntermediateDirectories:YES attributes:nil error:nil];
    [[NSFileManager defaultManager] createDirectoryAtPath:self.logDir withIntermediateDirectories:YES attributes:nil error:nil];

    NSMenu* menu = [[NSMenu alloc] init];
    self.statusLine = [[NSMenuItem alloc] initWithTitle:@"Starting…" action:nil keyEquivalent:@""];
    self.statusLine.enabled = NO;
    [menu addItem:self.statusLine];
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Open Verified Twin Studio" action:@selector(openStudio:) keyEquivalent:@"o"].target = self;
    [menu addItemWithTitle:@"Show Data Folder" action:@selector(showData:) keyEquivalent:@""].target = self;
    [menu addItemWithTitle:@"Show Logs" action:@selector(showLogs:) keyEquivalent:@""].target = self;
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Reset Demo Data…" action:@selector(resetData:) keyEquivalent:@""].target = self;
    [menu addItem:[NSMenuItem separatorItem]];
    [menu addItemWithTitle:@"Quit Verified Twin Studio" action:@selector(terminate:) keyEquivalent:@"q"];
    self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSImage* glyph = [NSImage imageWithSystemSymbolName:@"checkmark.seal" accessibilityDescription:@"Verified Twin Studio"];
    [glyph setTemplate:YES];  // "template" is a C++ keyword in Objective-C++
    self.statusItem.button.image = glyph;
    self.statusItem.button.toolTip = @"Verified Twin Studio";
    self.statusItem.menu = menu;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        [self startStack];
    });
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
    [self openStudio:nil];
    return NO;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    [self stopStack];
}

@end

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        StudioLauncher* delegate = [[StudioLauncher alloc] init];
        app.delegate = delegate;
        // Minimal main menu so that Cmd-Q works while the app is active.
        NSMenu* main = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        [main addItem:appItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Open Verified Twin Studio" action:@selector(openStudio:) keyEquivalent:@"o"].target = delegate;
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:@"Quit Verified Twin Studio" action:@selector(terminate:) keyEquivalent:@"q"];
        appItem.submenu = appMenu;
        app.mainMenu = main;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        // SIGTERM/SIGINT (kill, logout scripts) quit cleanly: every component is stopped.
        static dispatch_source_t sources[2];
        const int signals[2] = {SIGTERM, SIGINT};
        for (int i = 0; i < 2; ++i) {
            signal(signals[i], SIG_IGN);
            sources[i] = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, signals[i], 0, dispatch_get_main_queue());
            dispatch_source_set_event_handler(sources[i], ^{
                [NSApp terminate:nil];
            });
            dispatch_resume(sources[i]);
        }
        [app run];
    }
    return 0;
}
