# CommonUtils

[![CI](https://github.com/festensoft/CommonUtils/actions/workflows/ci.yml/badge.svg)](https://github.com/festensoft/CommonUtils/actions/workflows/ci.yml)

The small C++20 library of things I got tired of rewriting: a logger that works
before you've configured it, a few thread-safe building blocks, and a crash
handler that tells you *where* it died instead of just "Segmentation fault
(core dumped)".

Nothing here is revolutionary. It's just the stuff every project ends up needing,
written once, tested properly, and run under AddressSanitizer, UBSan and
ThreadSanitizer (as all respectable projects should be!).

## What's in the box

| Thing | What it does |
|---|---|
| **Logger** (`Logger.h`, `LoggerMacros.h`, ...) | Leveled, formatted logging with console, rotating-file and custom sinks. [Details below](#the-logger). |
| **`CircularBuffer<T>`** | Fixed-size, thread-safe ring buffer. Push forever, it only remembers the last N. Great for "show me the recent history" without growing without bound. |
| **`Timer`** | One-shot or periodic callback on its own thread. Cancels cleanly, and the callback may stop or re-arm its own timer without deadlocking. |
| **`SnoozableTimer`** | A watchdog-style timer: fires after a period unless you `Snooze()` it first, which pushes the deadline back. Think "do something if nothing has happened for 5 seconds". |
| **`DataHandler<T>`** | A thread-safe queue with listeners. Producers call `SignalData()`, and registered callbacks run on a dedicated worker thread, so the producer never waits on slow consumers. |
| **`StackTrace`** | Installs handlers for SIGSEGV / SIGABRT / SIGBUS that print a demangled stack trace, run your cleanup hook, then let the process die properly (core dump intact). Also gives you `CaptureStackTrace()` for logging from caught exceptions. |

Examples (logging mentioned later):

```cpp
CommonUtils::CircularBuffer<int> history(5);
for (int i = 1; i <= 8; ++i) history.Push(i);
// holds 4 5 6 7 8; the first three were politely evicted
auto snapshot = history.ToVector();   // consistent copy, oldest -> newest

CommonUtils::DataHandler<std::string> handler;
const int id = handler.RegisterListener(
    [](const std::string& msg) { GPINFO("got: {}", msg); });
handler.SignalData("hello");          // returns immediately; callback runs on the worker thread
handler.UnregisterListener(id);

CommonUtils::SnoozableTimer watchdog([] { GPWARN("nothing happened!"); }, 5000);
watchdog.Start();
watchdog.Snooze();                    // "I'm alive": deadline is now 5 s from now
```

Everything lives in `namespace CommonUtils`. Threading rules are documented in
each header, and they are specific: e.g. `Timer` callbacks can safely call
`Stop()` on their own timer, but two outside threads fighting over the same
`Timer` is on you.

## The logger

The logger is the most feature-rich part of the library, with a few spicy bits.

### Overview

```cpp
#include "LoggerMacros.h"

int main()
{
    GPINFO("Hello, {}!", "world");   // works immediately, no setup
}
```

That is the whole pitch: **logging works from the first line of `main()`**. Until
you configure it, messages go to a colored console logger. No init call, no
"logger not initialized" crash, no null pointer waiting in the dark.

When you're ready for something fancier, hand it a config (Config File TBD!):

```cpp
#include "LoggerConfig.h"
#include "LoggerMacros.h"

namespace Log = CommonUtils::Logging;

Log::LoggerConfig config;
config.level   = "debug";                       // the logger-wide gate
config.pattern = "[%H:%M:%S.%e] [%^%l%$] %v";   // spdlog pattern syntax

Log::SinkConfig console;
console.type    = Log::SinkType::Console;
console.level   = "info";                       // this sink only shows info and up
console.colored = true;
config.sinks.push_back(console);

Log::SinkConfig file;
file.type             = Log::SinkType::RotatingFile;
file.level            = "debug";
file.filePath         = "app.log";
file.maxFileSizeBytes = 5 * 1024 * 1024;        // rotate at 5 MB...
file.maxFiles         = 3;                      // ...and keep 3 files
config.sinks.push_back(file);

if (!Log::InitFromConfig(config))
{
    GPERROR("Logger config rejected; still using the default logger");
}
```

### The macros

`GPTRACE`, `GPDEBUG`, `GPINFO`, `GPWARN`, `GPERROR`, `GPCRIT`. They take a
`std::format` string:

```cpp
GPINFO("Connected to {} on port {}", host, port);
GPERROR("Failed after {} retries: {}", retries, reason);
```

Two things worth knowing:

- **The format string is checked at compile time.** Wrong number of arguments, or
  an argument of the wrong type? That's an easily fixed build error.
  The flip side: the first argument must be a string *literal*. To log a runtime
  string, pass it as an argument: `GPINFO("{}", message)`, never
  `GPINFO(message)`.
- **Disabled levels cost almost nothing.** The macros check the level first, so
  if debug is off, the arguments aren't evaluated and nothing is formatted.
  I still wouldn't commit a `GPTRACE` inside a tight loop.

Each message carries its file, line and function, so patterns like `%s:%#` work.

### Levels and the two-gate system

A message has to pass **two** gates to be written somewhere:

1. the logger's `level` (`config.level`), then
2. each sink's own `level`.

So you can run the logger at `trace`, send `info` and up to the console, and
write everything to a file, all at once. Valid level names: `trace`, `debug`,
`info`, `warn`, `error`, `critical`, `off`.

### What `InitFromConfig` does and doesn't do

- It returns `true` on success. It returns **`false`** (and leaves the current
  logger alone, logging the reason through it) if there are no usable sinks, a
  level name is misspelled, a custom sink isn't registered, or a file can't be
  opened. It doesn't throw.
- Call it again and the new config replaces the old one, with a warning. (The old
  logger is deliberately kept alive rather than freed. A thread might be mid-log
  on it, and keeping it makes the hot path lock-free. It's a few KB, once.)
- Errors and above are flushed immediately; use `Logging::Flush()` before exit if
  you want everything else out the door.
- If you use a colored console sink and your pattern has no `%^ ... %$` markers,
  the whole line gets colored. Add the markers yourself to color only part of it.
- Unit tests that reconfigure the logger can call `Logging::Shutdown()` in teardown
  to get back to the default logger.

spdlog does the heavy lifting underneath, but it's a private implementation
detail. None of its headers leak into yours, so you don't need it on your include
path.

### Custom sinks

Want log lines going to a GUI widget, a socket, or a rubber duck? Implement
`ICustomSink` and register a factory under a name:

```cpp
#include "CustomSink.h"

class WidgetSink : public CommonUtils::Logging::ICustomSink
{
public:
    void Log(CommonUtils::Logging::Level level, const std::string& message) override
    {
        // level tells you warning from error, so you can color things sensibly
    }
    void Flush() override {}
};

// At namespace scope, this registers before main() runs:
static CommonUtils::Logging::SinkRegistrar registrar(
    "widget", [](const CommonUtils::Logging::SinkConfig&)
    { return std::make_shared<WidgetSink>(); });

// Then in your config:
//   sink.type = SinkType::Custom;  sink.name = "widget";
```

The sink receives each message already formatted with your pattern. One gotcha: if
your sink lives in a *static library* and nothing else in its object file is
referenced, the linker may drop the file and the registration silently vanishes.
`CustomSink.h` has the details; the short version is to register explicitly from
`main` if you hit it.

### The trace logger (a flight recorder for crashes)

Some bugs only show up as "it crashed, and the last 100 things it did would have
told me why". Writing all of that to disk, at debug level, all day long is wasteful,
so there's a cheaper trick:

```cpp
config.enableTraceLogger   = true;
config.traceBacktraceDepth = 128;          // remember the last 128 messages
config.traceFilePath       = "trace.log";  // optional; the dump always goes to the console too
```

With that on, **every** message you log, at **every** level, from `GPTRACE` to
`GPCRIT`, is also dropped into a ring buffer in memory, whatever your logger and
sink levels are. Nothing is written from that buffer as it happens, and your normal
logs behave exactly as they did before: if the logger is at `warn`, the files still
only get `warn` and up. The buffer just quietly remembers the last N messages,
including all the `debug` chatter you filtered out. When something goes wrong, you
ask for it:

```cpp
CommonUtils::Logging::DumpTraceBacktrace();   // writes the last N messages and flushes
```

The dump comes out between `Backtrace Start` and `Backtrace End` marker lines, so it's
easy to spot in a busy console. Each message keeps its original level, so you can tell
the warning that preceded the crash from the debug noise around it.

One cost to be aware of: while the recorder is on, every `GP*` call is formatted and
recorded, even the ones the normal logger would have skipped. That's the price of a
complete flight recording, so it's a good fit for crash-prone builds and diagnostics,
and something to think about for a hot loop.

Hook it into the crash handler and you get a stack trace *and* the moments leading
up to it:

```cpp
CommonUtils::StackTrace::SetPostCrashHook([](int sig)
{
    GPCRIT("Caught signal {}", sig);
    CommonUtils::Logging::DumpTraceBacktrace();
    CommonUtils::Logging::Flush();
});
CommonUtils::StackTrace::InstallSignalHandlers();
```

A word of honesty: running arbitrary code inside a signal handler is inherently
dicey. If the crash happened while the logger or the heap was mid-operation, the
hook can deadlock. The stack trace is written *first*, using only safe calls, and a
3-second watchdog kills the process if the hook hangs, so the worst case is losing
the trace dump, not the stack trace.

## Building

You need CMake 4.1+, Ninja, and a C++20 compiler with a working `<format>`: GCC 13+
or a recent Clang (Apple Clang on macOS, or Clang with libstdc++ 13+ on Linux).
Dependencies (spdlog, GoogleTest) are used from the system if found and fetched
otherwise.

Everything goes through CMake presets, so there are no flags to remember:

```bash
cmake --workflow --preset debug-clang   # configure + build + test (macOS / Clang)
cmake --workflow --preset debug         # same, with the default compiler (GCC on Linux)
```

| Preset | What you get |
|---|---|
| `debug`, `release` | Default compiler (GCC in the Linux CI image) |
| `debug-clang`, `release-clang` | Clang (Apple Clang on macOS; `clang++` on Linux) |
| `asan-ubsan` | Clang + AddressSanitizer + UBSan |
| `tsan` | Clang + ThreadSanitizer |
| `clang-tidy` | Clang, with clang-tidy running as part of the build |

On macOS, use `debug-clang` / `release-clang`. The clang presets pin
`/usr/bin/clang++` so Homebrew CMake cannot pick Homebrew LLVM by accident —
mixing that Clang with Apple's libc++ fails at link time.

Each preset builds into `build/<preset>/`. The sanitizer and tidy presets are the
ones CI leans on, so if you break something, they'll tell you.

Want the exact toolchain the project is developed against? The repo has a
`.devcontainer`. Open it in VS Code and everything (clang, clangd, gdb, valgrind,
CMake) is already there. Editor squiggles come from clangd, which reads the
`compile_commands.json` that configuring a preset links into the repo root.

### Using it in your project

```cmake
include(FetchContent)
FetchContent_Declare(CommonUtils
    GIT_REPOSITORY https://github.com/festensoft/CommonUtils.git
    GIT_TAG        main)
FetchContent_MakeAvailable(CommonUtils)

target_link_libraries(my_app PRIVATE CommonUtils)
```

The headers are included by plain name (`#include "Logger.h"`), which the CMake
target sets up for you. When used as a subproject, CommonUtils skips its own tests
if you set `BUILD_TESTING` off, and its examples with
`-DCOMMONUTILS_BUILD_EXAMPLES=OFF`.

## The example program

There's one example app, `CommonUtilsExample`, and it's meant to be read as well as
run: each demo prints what it's doing as it goes, so you can watch the output and
the source side by side.

```bash
cmake --workflow --preset debug
./build/debug/bin/CommonUtilsExample            # the safe tour
./build/debug/bin/CommonUtilsExample --crash    # ...plus a deliberate crash
```

It runs these demos in order (sources in `examples/`):

1. **Logger** (`LoggerDemo.cpp`). Logs a couple of lines *before* any
   configuration, so you can see the colored default logger working. Then it
   configures a console sink plus a rotating file sink at trace level with a custom
   pattern, and logs at every level. It leaves `example_app.log` in the directory
   you ran it from. From here on, all the other demos log through this
   configuration, so their output is in the log file too.
2. **CircularBuffer** (`CircularBufferDemo.cpp`). Pushes 1 to 8 into a buffer of 5,
   shows the oldest three being evicted, then has a producer thread hammer the
   buffer while the main thread reads it, to show it holding up under concurrent use.
3. **Timer** (`TimerDemo.cpp`). A one-shot that fires after 100 ms, then a periodic
   timer ticking every 80 ms until it's stopped.
4. **SnoozableTimer** (`SnoozableTimerDemo.cpp`). Starts a 150 ms timer, snoozes it
   at 80 ms, and shows it firing ~150 ms *after the snooze* rather than after the
   start.
5. **DataHandler** (`DataHandlerDemo.cpp`). Registers a listener, signals two
   messages, and watches them arrive on the worker thread. Also shows the listener
   and queue counts.
6. **StackTrace** (`StackTraceDemo.cpp`). Always: throws and catches an exception
   and prints a demangled stack trace from the catch block. With `--crash`: installs
   the crash handlers, then dereferences a null pointer on purpose. You'll see the
   banner, the signal name, the stack trace, and the post-crash hook's message on
   stderr, and then the process dies of SIGSEGV exactly as it would have anyway.
   It runs last because, well, it's dead afterwards.

Run without `--crash` and it finishes with "All demos completed." Run with it and
that line never appears. That's the point.

## License

Copyright 2026 Steven Festenstein. Released under the
[Boost Software License 1.0](LICENSE). Do pretty much whatever you like; just keep
the license notice in source distributions.
