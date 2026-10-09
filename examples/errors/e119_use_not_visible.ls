// E119: uses are not re-exported (section 2.6, D3) - this module uses carve.ls,
// which uses schema.ls, but does not see schema.ls's grid itself (section 7.3, check 8).
// @expect error
// @expect stderr-contains grid 'level' is not visible here
use "../modules/carve.ls"
rule mark { level[R] => level[W] }
sequence main {
    once carve
    everywhere mark
}
