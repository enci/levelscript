// E115: a sequence application takes a count only - no policy= (section 7.3, check 37).
// @expect error
// @expect stderr-contains 'policy=' is not valid on sequence 's'
tag t { a }
layers { g: grid of t }
rule fill { g[.] => g[a] }
sequence s { all fill }
sequence main { resize(2, 2)  all(policy=incremental) s }
