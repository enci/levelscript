// @expect error
// @expect stderr-contains duplicate
layers { }
program { resize(1,1) }
program { resize(2,2) }
