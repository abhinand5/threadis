# thREaDIS - Design Doc

## Core Idea

Redis is stupid fast, but it's single-threaded, which means it doesn't use all cores on modern machines. It also has memory fragmentation issues with large values.

Solution? Run multiple Redis instances, each pinned to its own CPU core, with a smart proxy (perhaps smart is too much at this point) in front. 

## Why This Works

1. **Memory isolation** - Each Redis process has its own memory space, so fragmentation in one doesn't mess up others (lol that's what my tiny mind thinks at this point)

2. **Core utilization** - Each instance gets its own CPU core, no GIL-type bullshit to deal with

3. **Unix domain sockets** - WAY faster than TCP/IP for local IPC, roughly 2-3x throughput I believe.

## Architecture 

```
Client → Proxy → Multiple Redis Instances
```

The proxy handles:
- Key hashing (which Redis gets which key)
- Command routing
- Response aggregation (for multi-key commands)

Each Redis instance:
- Runs stock Redis (no code mods)
- Pinned to specific CPU core
- Memory limits enforced 
- Listens on Unix domain socket

## Commands Flow

1. Client sends command (`SET foo bar`)
2. Proxy hashes key (`foo`) to determine instance
3. Proxy routes command to that instance
4. Instance processes command
5. Response flows back through proxy

## Special Cases

Some commands need to span instances (`MGET`, `MSET`). The proxy splits these into multiple single-instance commands and aggregates results.

## Next Steps

1. Benchmark the shit out of this
2. Add connection pooling
3. Smart handling of Redis commands (especially multi-key ones)
4. Persistence strategy across instances

## Maybe Later

- Shared memory if Unix domain sockets aren't fast enough
- Admin dashboard to see per-instance stats
- Custom config to tune per workload

## Inspiration

Inspired by Redis's own single-threaded non-blocking IO concurrency model, and by my (probably stupid) curiosity to explore multi-core scaling. Also, I just wanted an excuse to start an OSS project that might actually be useful! Let's see if this experiment pays off...
