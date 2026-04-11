# CMake DAP Feature Reference

Documented from CMake 4.3.1 source (`Source/cmDebugger*.cxx`).

## Requests (16)

| Request | Notes |
|---|---|
| `initialize` | Returns capabilities and CMake version |
| `launch` | Stubbed (no-op) |
| `configurationDone` | Signals end of configuration phase |
| `disconnect` | Terminates session |
| `continue` | Resumes execution |
| `next` | Step over (tracks call depth) |
| `stepIn` | Step into function call |
| `stepOut` | Step out of current function |
| `pause` | Pause execution |
| `threads` | Single thread ("CMake script") |
| `stackTrace` | Supports format options (parameters, line) |
| `scopes` | Single scope: "Locals" |
| `variables` | Read-only, supports `variablesReference` nesting |
| `setBreakpoints` | Line breakpoints only |
| `setExceptionBreakpoints` | Filter-based (see below) |
| `exceptionInfo` | Returns exception details when stopped |
| `evaluate` | Variable name lookup only, no expressions |

## Capabilities

Only three capabilities advertised as true:

- `supportsConfigurationDoneRequest`
- `supportsValueFormattingOptions`
- `supportsExceptionInfoRequest`

Everything else defaults to false, including:
`supportsConditionalBreakpoints`, `supportsHitConditionalBreakpoints`,
`supportsFunctionBreakpoints`, `supportsDataBreakpoints`,
`supportsLogPoints`, `supportsSetVariable`, `supportsSetExpression`,
`supportsEvaluateForHovers`, `supportsCompletionsRequest`,
`supportsRestartRequest`, `supportsModulesRequest`,
`supportsDisassembleRequest`.

## Events

| Event | Reasons |
|---|---|
| `initialized` | After initialize response |
| `stopped` | `breakpoint`, `step`, `pause`, `exception` |
| `breakpoint` | `changed` (verified/invalidated on file load) |
| `thread` | `started`, `exited` |
| `exited` | Includes exit code |
| `terminated` | Session complete |

## Breakpoints

- **Line breakpoints only.** No conditional, hit count, function, data, or
  log point breakpoints.
- Lines are calibrated to function boundaries when the source file loads.
- Breakpoints set before a file loads are stored and validated
  asynchronously. A `breakpoint changed` event fires when verified.

## Exception Filters

Nine filters, four enabled by default:

| Filter | Default |
|---|---|
| `FATAL_ERROR` | enabled |
| `INTERNAL_ERROR` | enabled |
| `AUTHOR_ERROR` | enabled |
| `DEPRECATION_ERROR` | enabled |
| `AUTHOR_WARNING` | disabled |
| `WARNING` | disabled |
| `DEPRECATION_WARNING` | disabled |
| `MESSAGE` | disabled |
| `LOG` | disabled |

When a matching message occurs and the filter is enabled, execution
pauses with `stopped` reason `exception`.

## Variables and Scopes

- Single scope: **Locals** (`presentationHint: "locals"`).
- Variables include `name`, `value`, `type` (when client advertises
  `supportsVariableType`), and `variablesReference` for nesting.
- **Read-only.** `setVariable` and `setExpression` are not supported.

## Evaluate

The `evaluate` request performs a variable name lookup in the current
frame (`Makefile->GetDefinition()`). It does not evaluate expressions,
arithmetic, or function calls. Always returns `type: "string"`.

## Threading

CMake is single-threaded. The `threads` request returns one thread.
All execution control and stack operations target this single thread.

## Limitations

- No attach mode (launch only, and launch is a no-op stub).
- No variable modification.
- No expression evaluation beyond variable lookup.
- No conditional or advanced breakpoint types.
- No module, source, or disassembly requests.
- No completions or hover support.
- Breakpoint line numbers may shift to function boundaries.
- Exception info is cleared after the `exceptionInfo` request.
