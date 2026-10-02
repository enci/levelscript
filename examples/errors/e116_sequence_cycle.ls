// E116: sequences may not apply themselves, directly or through others (§7.3 #38).
// @expect error
// @expect stderr-contains sequence cycle: 'a' -> 'b' -> 'a'
sequence a { one b }
sequence b { some(max=2) a }
layers { }
sequence main { resize(1, 1)  one a }
