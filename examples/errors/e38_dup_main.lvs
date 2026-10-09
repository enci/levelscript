// @expect error
// E38: two sequences named 'main' - rules and sequences share one namespace (section 7.3, check 39).
// @expect stderr-contains duplicate sequence 'main'
layers { }
sequence main { resize(1,1) }
sequence main { resize(2,2) }
