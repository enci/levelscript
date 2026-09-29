// @expect error
// @expect stderr-contains only one 'program' block
layers { }
program { resize(1,1) }
program { resize(2,2) }
