# CI for Debian Builds - Design Document

Issue: #7

## Summary

Add GitHub Actions CI to build and test the kernel module against multiple Debian releases (stable, testing, unstable) to ensure compatibility across kernel versions.

## Design Decisions

| Decision | Choice |
|----------|--------|
| Static analysis | Deferred - start minimal with build verification only |
| Artifacts | Upload .ko file with kernel version in name and build-info.txt |
| Scheduled builds | Weekly (Monday 00:00 UTC) to catch kernel header updates |
| README badge | Yes, build status badge at top |

## Workflow Structure

**File:** `.github/workflows/build.yml`

**Triggers:**
- Push to `main` branch
- Pull requests to `main` branch
- Weekly schedule (Monday 00:00 UTC)

**Build Matrix:**

| Name | Container | Purpose |
|------|-----------|---------|
| stable | `debian:bookworm` | Debian 12, kernel ~6.1.x |
| testing | `debian:trixie` | Debian 13, kernel ~6.11.x+ |
| unstable | `debian:sid` | Latest kernels |

**Matrix Settings:**
- `fail-fast: false` - All three builds complete even if one fails

## Workflow Steps

1. **Checkout** - `actions/checkout@v4`

2. **Install Dependencies**
   ```bash
   apt-get update
   apt-get install -y build-essential linux-headers-amd64
   ```

3. **Find Kernel Headers** - Dynamically discover installed headers version
   ```bash
   KVER=$(ls /usr/src/ | grep -E '^linux-headers-[0-9]' | sort -V | tail -1 | sed 's/linux-headers-//')
   ```

4. **Build Module**
   ```bash
   make KDIR=/usr/src/linux-headers-$KVER
   ```

5. **Verify Build**
   ```bash
   modinfo cpu_capacity_mod.ko
   file cpu_capacity_mod.ko
   ```

6. **Generate Build Info**
   ```bash
   echo "Debian Release: <name>" > build-info.txt
   echo "Kernel Headers: $KVER" >> build-info.txt
   echo "Build Date: <timestamp>" >> build-info.txt
   echo "Git Commit: <sha>" >> build-info.txt
   echo "Git Branch: <ref>" >> build-info.txt
   ```

7. **Upload Artifacts** - Named `cpu_capacity_mod-debian-<name>-<kver>`
   - `cpu_capacity_mod.ko`
   - `modinfo.txt`
   - `build-info.txt`

## Key Technical Details

### Container Kernel Mismatch

Inside containers, `uname -r` returns the GitHub runner's host kernel (Ubuntu), not a Debian kernel. Solution:
- Install `linux-headers-amd64` meta-package
- Dynamically find installed headers in `/usr/src/`
- Pass explicit `KDIR` to make

### Expected Warning

The build produces this warning which is expected and not a failure:
```
WARNING: modpost: "cpu_scale" [cpu_capacity_mod.ko] undefined!
```
The `KBUILD_MODPOST_WARN=1` in the Makefile ensures this doesn't fail the build.

## Files to Create/Modify

1. **Create:** `.github/workflows/build.yml` - Main CI workflow
2. **Modify:** `README.md` - Add build status badge

## Future Enhancements (Deferred)

- Static analysis with `sparse`
- Kernel coding style check with `checkpatch.pl`
- Build on tag creation for releases
