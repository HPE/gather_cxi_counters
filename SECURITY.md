# Security Release Process

gather_cxi_counters is a utility for collecting Slingshot/CXI NIC performance
counters on large HPE systems. The project has adopted this security disclosure
and response policy to ensure we responsibly handle critical issues.

## Supported Versions

gather_cxi_counters maintains the latest release on the `main` branch. Security
fixes will be applied to the current release.

| Version | Supported          |
| ------- | ------------------ |
| latest  | :white_check_mark: |

## Reporting a Vulnerability — Private Disclosure Process

Security is of the highest importance and all security vulnerabilities or
suspected security vulnerabilities should be reported to gather_cxi_counters
privately, to minimize attacks against current users before they are fixed.
Vulnerabilities will be investigated and patched on the next release as soon
as possible.

**IMPORTANT: Do not file public GitHub issues for security vulnerabilities.**

### How to Report

Use **GitHub Security Advisories** to report vulnerabilities privately:

1. Navigate to the [Security tab](../../security) of this repository
2. Click **"Report a vulnerability"**
3. Fill out the advisory form with the details below

This allows for private discussion and coordinated disclosure without requiring
email.

### Alternative Contact

If you cannot use GitHub Security Advisories, contact
[github@hpe.com](mailto:github@hpe.com).

### Information to Include

Please provide as much of the following information as possible:

- Your name and affiliation (optional but helpful)
- Type of issue (e.g., privilege escalation via debugfs, path traversal in
  output file handling, unsafe use of shell expansion, credential leakage in
  log output, dependency vulnerability, etc.)
- Full paths of source file(s) related to the issue
- The location of the affected source code (tag/branch/commit or direct URL)
- Any special configuration required to reproduce the issue (OS, kernel version,
  CXI driver version, privilege level)
- Detailed steps to reproduce the vulnerability
- Proof-of-concept or exploit code (if possible)
- Impact assessment: what an attacker can do, prerequisites, affected
  environments

This information will help us triage your report more quickly. Please report as
soon as possible even if some information cannot be immediately provided.

## When to Report a Vulnerability

- When you think gather_cxi_counters has a potential security vulnerability
- When you suspect a potential vulnerability but are unsure that it impacts
  gather_cxi_counters
- When you know of or suspect a potential vulnerability in a dependency used by
  gather_cxi_counters

## Scope and Threat Model Notes

Because gather_cxi_counters reads kernel sysfs/debugfs counters and is typically
run with elevated privileges on HPC nodes, the most likely security risks include:

- **Privilege escalation**: Any code path that invokes subprocesses or handles
  user-supplied filenames must not be exploitable to escalate privileges beyond
  what is explicitly required
- **Path traversal**: Output file or log-file paths derived from user input or
  node names must be sanitized
- **Unsafe shell expansion**: Counter names or node names must not be passed
  unsanitized into shell commands
- **Sensitive data in output**: Node hostnames, topology information, or counter
  values may be considered sensitive in certain deployment contexts; the tool
  should not log credentials or secret tokens

## Patch, Release, and Disclosure

The gather_cxi_counters maintainers will respond to vulnerability reports as
follows:

1. Investigate the vulnerability and determine its effects and criticality
2. If the issue is not deemed to be a vulnerability, follow up with a detailed
   reason for rejection
3. Initiate a conversation with the reporter within **3 business days**
4. If a vulnerability is acknowledged, work on a plan to communicate with the
   community, including identifying mitigating steps that affected users can
   take immediately
5. Create a [CVSS](https://www.first.org/cvss/specification-document) score
   using the [CVSS Calculator](https://www.first.org/cvss/calculator/3.0) if
   applicable
6. Work on fixing the vulnerability and perform internal testing before rolling
   out the fix
7. A public disclosure date is negotiated with the bug submitter. We prefer to
   fully disclose the bug as soon as possible once a mitigation or patch is
   available

### Public Disclosure Process

Upon release of the patched version, we will publish a public
[advisory](../../security/advisories) to the gather_cxi_counters community via
GitHub. The advisory will include any mitigating steps users can take until the
fix can be applied.

## Confidentiality, Integrity, and Availability

We consider vulnerabilities leading to the compromise of data confidentiality,
elevation of privilege, or integrity to be our highest priority concerns.
Availability, particularly relating to resource exhaustion on production HPC
nodes, is also a serious security concern.

## Preferred Languages

We prefer all communications to be in English.

## Policy

Under the principle of Coordinated Vulnerability Disclosure, researchers
disclose newly discovered vulnerabilities directly to the maintainers privately.
The researcher allows the maintainers the opportunity to diagnose and offer
fully tested updates, workarounds, or other corrective measures before any party
discloses detailed vulnerability or exploit information to the public.