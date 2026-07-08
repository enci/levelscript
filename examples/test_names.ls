// @seed 42
// Names-only fill/clear demo (v0.2: tag values are referenced by name only).
// Fill every empty cell with wall, then clear all walls back to empty.
// All 16 cells end empty.
// @expect grid level count(w) == 0
// @expect grid level count(.) == 16

tag terrain { wall, floor }
layers { level: grid of terrain }

rule fill  { level[.]    => level[wall] }
rule clear { level[wall] => level[.] }

program {
    resize(4, 4)
    all fill
    all clear
}
