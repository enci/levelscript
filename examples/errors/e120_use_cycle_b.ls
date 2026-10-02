// E120 (other half): compiled on its own, this module cycles too (section 7.3, check 40).
// @expect error
// @expect stderr-contains module cycle
use "e120_use_cycle_a.ls"
