// Modules (§2.6): the root of a three-module closure. It uses carve.ls only,
// so it may apply `carve` but not name `level` - that would need its own
// `use "modules/schema.ls"`. Tools run the sequence `main` (§6).
// @expect run-ok
use "modules/carve.ls"

sequence main {
    one carve
}
