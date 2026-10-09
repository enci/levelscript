// E116: sequences may not apply themselves, directly or through others (section 7.3, check 38).
// @expect error
// @expect stderr-contains sequence cycle: 'a' -> 'b' -> 'a'
sequence a { once b }
sequence b { settle(2) a }
layers { }
sequence main { resize(1, 1)  once a }
