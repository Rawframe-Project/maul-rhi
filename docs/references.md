# References

Published sources the library's algorithms are implemented from, as
the family's conventions ask (section 17).

- **SHA-256:** NIST, "Secure Hash Standard (SHS)", FIPS PUB 180-4,
  2015. `src/sha256.c`, for the shader container's digest.
- **TLSF:** M. Masmano, I. Ripoll, A. Crespo and J. Real, "TLSF: a New
  Dynamic Memory Allocator for Real-Time Systems", Proceedings of the
  16th Euromicro Conference on Real-Time Systems (ECRTS), 2004.
  `src/tlsf.c`, suballocating GPU memory in the Vulkan driver. The
  exact-fit scan of a size's own class is the library's addition.
