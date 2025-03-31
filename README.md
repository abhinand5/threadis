# thREaDIS
A high-performance Redis proxy that shards keys across multiple isolated Redis instances using Unix domain sockets for ultra-fast local caching.

**Alternative to Redis Cluster:** thREaDIS focuses on efficient multi-core scaling on a single machine rather than horizontal scaling across multiple servers. It's designed for environments where you need to maximize performance on a single powerful server before adding more machines.

> **Note** This project is a PoC and is clearly at its infancy. Wait for a month to see reasonable progress (only if everything goes to plan)

Check out the [design document](docs/poc/DESIGN.md) for more details on how this ~~works~~ could work.

PS: I am a noob trying to learn something by doing, be kind, this perhaps may not make sense to senior devs and may be a completely ridiculous idea, I don't care, I'm sure there's something to learn.

That said this is starting with just me, collaborations are welcome.
