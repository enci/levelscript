// Showcase: counts are statement-scope expressions over params (section 6).
// `budget` scales how many rocks scatter and how far the walkers grow;
// `cover` is a percentage of one batch. A computed count below 0 acts as 0
// and a percentage above 100 as 100, without an error.
// @expect grid ground count(rock) == 6
tag geo { dirt, rock, path }
layers { ground: grid of geo }
params {
    budget: number = 60
    cover: number = 25
}
rule fill  { ground[.] => ground[dirt] }
rule rocks { ground[dirt] => ground[rock] }
rule start { ground[dirt] => ground[path] }
rule walk(rotation=all) { ground[path dirt] => ground[* path] }
sequence main {
    resize(12, 8)
    everywhere fill
    scatter(budget / 10) rocks
    once start
    grow(budget / 3) walk
    scatter((cover * 2)%) start
}
