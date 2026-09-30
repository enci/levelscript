// E115: a sequence application takes a count only - no policy= (§7.3 #37).
// @expect error
// @expect stderr-contains 'policy=' is not valid on sequence 's'
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence s { all fill }
program { resize(2, 2)  all(policy=incremental) s }
