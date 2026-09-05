# Testing

Before reporting a test failure, follow the canonical procedure in
[`docs/development/testing.md`](../docs/development/testing.md).

Do not treat a bare `DeepRunTests.exe` launch as equivalent to CTest: the
registered test supplies both its output-directory working directory and
`--asset-root Assets`. For milestone acceptance, report the exact Debug and
Release CTest commands and results.
