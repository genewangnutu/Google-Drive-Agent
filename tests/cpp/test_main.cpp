// Single, central provider of doctest's main() for the whole gda_tests
// binary. Every other test_*.cpp must only `#include <doctest/doctest.h>`
// (no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN of its own) -- otherwise linking
// them together duplicates main()/doctest's runtime.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
