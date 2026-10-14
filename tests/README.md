# SocketCAN transport lifetime tests

Driver baseline: e4988d0404321d93d2ec732641cf377bf0ca9692.
Core baseline: 5b7b9fc3ccc18e30ebfba92da6486cffc6251595 (unchanged).

On Linux, from the driver checkout:

```sh
bash tests/run.sh /absolute/path/to/verified/NMEA2000 /absolute/path/to/disposable/output
```

Requires GCC, Linux headers, AddressSanitizer and UndefinedBehaviorSanitizer.
The linker wraps socket/ioctl/bind/fcntl/close/read/write/select in the production
driver translation unit. No CAN socket is created. Acquisition uses real Linux
/dev/null descriptors; successful close uses real close(2). Failure stages are
injected deterministically. Unexpected read/write/select calls abort the test.
Flags are applied using real fcntl, and closed descriptors are checked with
F_GETFD. Counts detect missing and duplicate release. Sanitizers check memory
safety, including temporary interface-name storage. No sleeps or CAN devices.

Coverage: initial closed state; all five acquisition failure stages; recovery;
duplicate open; repeated close; destruction; closed reads/writes; copied names;
rejection of active interface changes; close error reporting; original opening
errno preservation even on cleanup error; 100 transport open/close cycles.
These tests call the production CANOpen hook through a derived test facade.
They do NOT establish core Open/ParseMessages restart correctness or CAN
interoperability. No full core opening is performed, avoiding its known lack
of teardown. Copying the owning driver is intentionally disabled.

Close is transport-only and requires serialized access: stop servicing and
sending before calling it. On Linux a close error must be reported but the
numeric descriptor must not be retried: it may already have been released and
reused. The base core destructor is nonvirtual; delete through the concrete
driver type, never through tNMEA2000*. Explicit Close remains necessary when
using the existing process-lifetime core instance.

Integration assessment:
- A retained core avoids repeated core allocation, but socket closure does not
  clear its IsOpen state, frame queue, address-claim state or pending responses.
- Public Restart only starts address claiming; it does not reset opening or
  queued traffic. A low-level CANOpen/Close cycle is not a full stack restart.
- Until a supported reset contract exists, terminal close and restart of the
  application is the safe integration boundary. Live interface/identity changes
  and full close/reopen remain unsupported. Do not call ParseMessages after closing.
- Applications must separate protocol servicing from Viewer mode,
  gate simulation sending, detach callbacks and stop timers before closing.
- Do not delete/recreate cores per session: core-owned buffers/device data and
  group-function handlers have no demonstrated complete destruction path.

The driver changes preserve frame encoding and receive/filter behavior. There
are no changes to the core, simulator source, dependency pins or verified stages.
