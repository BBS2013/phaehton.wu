# AGENTS.md

## Cursor Cloud specific instructions

This is a C++ DoIP (Diagnostics over IP / ISO 13400) simulation project located in `DoIP_Simulation/`.

### Build

```bash
cd DoIP_Simulation && make
```

Produces two binaries: `doip_server` and `doip_client`. See `DoIP_Simulation/README.md` for full protocol documentation.

### Running

1. Start the server first (it binds UDP + TCP on port **13400**):
   ```bash
   ./doip_server &
   ```
2. Then run the client, which performs a complete diagnostic session and exits:
   ```bash
   ./doip_client
   ```

### Caveats

- Port 13400 must be free before starting `doip_server`. If a previous server is still running, find its PID with `lsof -i :13400` and kill it by PID before restarting.
- The client sends a UDP broadcast for vehicle discovery; in some container network setups, the broadcast may resolve to the container's own IP rather than `127.0.0.1`. This is normal and does not affect functionality.
- There are no external dependencies, no package manager, and no linter configured. The only build prerequisite is `g++` with C++11 and pthread support.
- There are no automated test suites; end-to-end testing is done by running `doip_server` then `doip_client` and verifying the expected output (see README).
