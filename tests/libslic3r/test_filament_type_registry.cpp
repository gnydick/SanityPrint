#include <catch2/catch.hpp>

#include "libslic3r/FilamentTypeRegistry.hpp"

using namespace Slic3r;

// These tests pass whether the registry loads resources/info/filament_info.json or falls
// back to its built-in defaults: every type referenced here is present (with the same
// classification) in both, and the base relationships are either explicit or prefix-derived
// in both.

TEST_CASE("FilamentTypeRegistry classifies known types", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("PLA")  == LowTemp);
    REQUIRE(r.temp_type("ABS")  == HighTemp);
    REQUIRE(r.temp_type("PETG") == HighLowCompatible);
    REQUIRE(r.is_known("PLA"));
    REQUIRE(r.is_known("PETG"));
}

TEST_CASE("FilamentTypeRegistry resolves custom types through their base", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.base_type("PLA-Galaxy") == "PLA");
    REQUIRE(r.temp_type("PLA-Galaxy") == LowTemp);          // inherits PLA
    REQUIRE(r.temp_type("ABS-Custom") == HighTemp);         // inherits ABS
    REQUIRE(r.temp_type("PETG-Pro")   == HighLowCompatible);// inherits PETG
    REQUIRE_FALSE(r.is_known("PLA-Galaxy"));                // custom, not a built-in
}

TEST_CASE("FilamentTypeRegistry prefix inference is separator-guarded", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // "PLATINUM" starts with "PLA" but is not separated, so it must NOT inherit PLA.
    REQUIRE(r.base_type("PLATINUM").empty());
    REQUIRE(r.temp_type("PLATINUM") == Undefine);
    // ...whereas a proper separator does match.
    REQUIRE(r.base_type("PLA-Galaxy") == "PLA");
}

TEST_CASE("FilamentTypeRegistry normalizes case and whitespace", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("  pla ")     == LowTemp);   // trimmed + upper
    REQUIRE(r.temp_type("pla-galaxy") == LowTemp);
    REQUIRE(r.base_type("pla+")       == "PLA");      // '+' separator
    REQUIRE(r.base_type("PLA Tough")  == "PLA");      // ' ' separator
}

TEST_CASE("FilamentTypeRegistry: longest base wins (PCTG before PC)", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // PCTG is a base candidate; a PCTG-X custom must resolve to PCTG, not PC.
    REQUIRE(r.base_type("PCTG-X") == "PCTG");
    REQUIRE(r.base_type("PC-Custom") == "PC");
}

TEST_CASE("FilamentTypeRegistry effective_type: built-ins kept, customs collapse to base", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // Built-ins keep their own identity, so their exact downstream behavior is preserved.
    REQUIRE(r.effective_type("PLA")    == "PLA");
    REQUIRE(r.effective_type("PETG")   == "PETG");
    REQUIRE(r.effective_type("PLA-CF") == "PLA-CF");   // a known derived built-in, not collapsed
    // Custom types collapse to their base so they inherit behavior.
    REQUIRE(r.effective_type("PLA-Galaxy") == "PLA");
    REQUIRE(r.effective_type("PETG-Pro")   == "PETG");
    REQUIRE(r.effective_type("ABS-Custom") == "ABS");
    // A type with no recognizable base resolves to itself (normalized).
    REQUIRE(r.effective_type("zzz") == "ZZZ");
}

TEST_CASE("FilamentTypeRegistry: unknown and empty map to Undefine", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("ZZZ-Unobtanium") == Undefine);
    REQUIRE(r.temp_type("") == Undefine);
    REQUIRE(r.base_type("").empty());
}
