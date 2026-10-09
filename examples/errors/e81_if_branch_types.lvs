// E81: 'if' branches disagree in type (tag vs number).
// @expect error
// @expect stderr-contains branches
tag geometry { wall, floor }
layers { level: grid of geometry }
rule bad { level[.] => level[ (if(width > 1, wall, 3)) ] }
sequence main { resize(1,1) }
