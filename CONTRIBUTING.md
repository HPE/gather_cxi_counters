# Contributing to gather_cxi_counters

Thank you for your interest in contributing to gather_cxi_counters! This document
provides guidelines and instructions for contributing to this project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Community Communication](#community-communication)
- [Developer Certificate of Origin](#developer-certificate-of-origin)
- [Getting Started](#getting-started)
- [How to Contribute](#how-to-contribute)
- [Pull Request Process](#pull-request-process)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Other Ways to Contribute](#other-ways-to-contribute)
- [References](#references)

## Code of Conduct

This project adheres to our [Code of Conduct](CODE_OF_CONDUCT.md). By
participating, you are expected to uphold this code. Please report unacceptable
behavior to [github@hpe.com](mailto:github@hpe.com).

## Community Communication

We encourage community participation and welcome your contributions!

- **GitHub Issues**: For bug reports and feature requests
- **GitHub Discussions**: For questions, ideas, and general community discussion
- **Pull Requests**: For code contributions (see [Contributing Code](#contributing-code))

For general questions or to reach maintainers directly:
- **Email**: [github@hpe.com](mailto:github@hpe.com)

## Developer Certificate of Origin

gather_cxi_counters requires the Developer Certificate of Origin (DCO) process
for all contributions. The DCO is a lightweight way for contributors to certify
that they wrote or otherwise have the right to submit the code they are
contributing.

### What is the DCO?

The DCO is a declaration that you have the right to contribute the code and that
you agree to it being used under the project's license.

**Developer Certificate of Origin Version 1.1**

```
Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

Read the full [DCO text](https://developercertificate.org/).

### How to Sign Off

Every commit must include a `Signed-off-by` line with your name and email:

```
Signed-off-by: Your Name <your.email@example.com>
```

**Using git:**
```bash
# Add sign-off to a single commit
git commit -s -m "Your commit message"

# Add sign-off to multiple commits
git rebase HEAD~n --signoff  # where n is the number of commits
```

**Note**: Use your real name (no pseudonyms) and a working email address.

### Fixing Unsigned Commits

```bash
# Amend the last commit
git commit --amend --signoff

# For multiple commits, use interactive rebase
git rebase -i HEAD~N
# Then amend each commit with --signoff

# Force push after fixing (only on your feature branch!)
git push --force-with-lease
```

## Getting Started

1. **Fork the repository** on GitHub

2. **Clone your fork**:
   ```bash
   git clone https://github.com/YOUR_USERNAME/gather_cxi_counters.git
   cd gather_cxi_counters
   ```

3. **Install build dependencies**:
   - CMake ≥ 3.20
   - A C++20-capable compiler (GCC 11+, Clang 14+, or Cray PE Clang)
   - git (required for CMake FetchContent to pull libzmq)
   - Internet access at build time (or a local libzmq mirror; see README)

4. **Build the project**:
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j8
   ```
   This produces `build/gather_cxi_counters` (~966 KB, statically linked).

5. **Configure git sign-off** (for DCO):
   ```bash
   git config user.name "Your Name"
   git config user.email "your.email@example.com"
   ```

6. **Verify access** to a system with HPE Slingshot/CXI hardware for functional
   testing. Nodes with `cxi0`, `cxi1`, … visible under `/sys/class/cxi/` are
   required to exercise counter collection.

## How to Contribute

### Reporting Bugs

Before creating a bug report:

- Check the existing issues to avoid duplicates
- Collect relevant information:
  - OS/kernel version (`uname -r`)
  - CXI driver version (`modinfo cxi_core` or equivalent)
  - CXI device names (`ls /sys/class/cxi/`)
  - Full `srun` command invocation and complete output/error
  - Number of nodes and SLURM partition used

When creating an issue:
- Use a clear, descriptive title
- Provide detailed steps to reproduce
- Include expected vs actual behavior
- Attach relevant logs (set `GATHER_CXI_LOG=1` for verbose ZMQ output)
- Scrub any sensitive node names/IPs before attaching

### Suggesting Features

- Check existing issues/discussions first
- Describe the use case and benefit
- Consider whether the feature fits the project scope (Slingshot CXI counter
  collection, aggregation, and analysis for HPC workloads)

### Contributing Code

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** following our [Coding Standards](#coding-standards)

3. **Build and verify** (see [Testing](#testing))

4. **Commit with sign-off**:
   ```bash
   git commit -s -m "Add feature X"
   ```

5. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```

6. **Open a Pull Request**

## Pull Request Process

1. **Before submitting**:
   - Ensure all commits are signed (DCO)
   - Build cleanly with no warnings (`cmake .. -DCMAKE_BUILD_TYPE=Release && make -j8`)
   - Verify functional behavior on hardware if your change touches counter
     collection, aggregation, or ZMQ communication
   - Update README.md if you add or change environment variables or output format
   - Keep PRs focused and reasonably sized

2. **PR Description should include**:
   - Clear description of what the PR does
   - Why the change is needed
   - How it was tested (real CXI hardware, node count, SLURM partition)
   - Any breaking changes to output format or environment variable behavior

3. **Review process**:
   - A maintainer will review your PR
   - Address any feedback
   - Once approved, a maintainer will merge

## Coding Standards

### Language and Standard

- C++20 (`set(CMAKE_CXX_STANDARD 20)` in CMakeLists.txt)
- Target portability across GCC 11+, Clang 14+, and Cray PE compilers

### Style

- Use `clang-format` for consistent formatting. The codebase follows a style
  compatible with LLVM defaults. Run before committing:
  ```bash
  clang-format -i *.cpp *.h
  ```
- Use meaningful variable and function names
- Add comments to non-obvious code paths — particularly ZMQ message framing,
  counter sysfs path construction, and aggregation math

### Project-Specific Guidelines

- **Portability**: The tool must work on any node running `cxi_core`; avoid
  hard-coding paths where driver layout may vary across Slingshot generations
- **Environment variable interface**: Adding new behavior via environment
  variables is the established pattern; do not add positional CLI flags without
  discussion (the only current flag is `-e <experiment_name>`)
- **Output stability**: Counter names and column order in the text and JSON
  output are treated as a public interface — changes require a changelog entry
  and a deprecation notice
- **Static linking**: The binary is statically linked (libzmq is built via
  FetchContent with `BUILD_SHARED=OFF`). Preserve this property for portability
  across compute nodes that may have different system libraries
- **Plugin architecture**: New metric sources should implement the
  `MetricSource` abstract interface (`metric_source.h`) and be registered via
  `MetricRegistry`. Do not add metric collection logic directly to `main.cpp`

### Commit Messages

- Use clear, descriptive commit messages
- Start with a verb (Add, Fix, Update, Remove, Refactor, etc.)
- Keep the first line under 72 characters
- Reference issues when applicable: `Fixes #123`

Example:
```
Add per-NIC delta mode for rate calculation

- Read counters twice with a configurable interval
- Compute per-second rates for tx/rx bytes and packets
- Output both raw and delta columns in CSV mode

Fixes #7

Signed-off-by: Your Name <your.email@example.com>
```

## Testing

gather_cxi_counters does not currently have an automated unit test suite.
The primary verification method is a clean build followed by a functional
test on a system with CXI hardware.

### Build verification

After any change, confirm the project builds cleanly:

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
```

A successful build with no compiler errors or warnings is the minimum bar for
any pull request.

### Functional testing on hardware

If your change affects counter collection, aggregation, or output formatting,
test it on a node with CXI hardware before submitting:

```bash
# Single node, text output
srun -N 1 -n 1 ./gather_cxi_counters sleep 2

# Single node, JSON output
GATHER_CXI_JSON=1 srun -N 1 -n 1 ./gather_cxi_counters sleep 2

# Multi-node (2+ nodes)
srun -N 2 -n 2 --ntasks-per-node=1 ./gather_cxi_counters sleep 2

# With a real workload that generates traffic
srun -N 2 -n 2 --ntasks-per-node=1 ./gather_cxi_counters ./your_mpi_benchmark
```

Include the output from at least the single-node and multi-node runs in your
PR description.

### What to check

- Binary builds without errors or warnings
- Single-node text and JSON output are correctly formatted
- Multi-node run completes without ZMQ errors or hangs
- Counter table is populated when a network-active workload is wrapped
- New environment variables take effect as documented

## Other Ways to Contribute

- **Documentation**: Improve README, add counter interpretation examples,
  fix typos, add example output from real workloads
- **Testing**: Test on different Slingshot generations or larger node counts
  and report results as issues or discussions
- **Bug Reports**: Report issues with detailed reproduction steps
- **Feature Ideas**: Suggest new metric sources, output formats, or
  integration hooks via GitHub Discussions
- **Spread the Word**: Star the repository, share with HPC colleagues

## References

- [Developer Certificate of Origin](https://developercertificate.org/)
- [GitHub Flow Guide](https://guides.github.com/introduction/flow/)
- [HPE Slingshot documentation](https://www.hpe.com/us/en/compute/hpc/slingshot-interconnect.html)
- [CXI kernel driver (cxi-driver)](https://github.com/HewlettPackard/shs-cxi-driver)
- [ZeroMQ documentation](https://zeromq.org/documentation/)

---

By contributing to gather_cxi_counters, you agree that your contributions will
be licensed under the project's [MIT License](LICENSE.md).
