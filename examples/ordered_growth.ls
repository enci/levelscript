// @seed 14
// Ordered + incremental = preemptive priority (the MarkovJunior loop):
// extending an existing path always beats starting a new one, so exactly
// one germinated path snakes across the grid until nothing can extend.
tag algo { head, trail }

layers {
    algo: grid of algo
}

rule snake(rotation=all) { ordered
    algo[head .] => algo[trail head]
    algo[.]      => algo[head]
}

program {
    resize(12, 8)
    some(max=40, policy=incremental) snake
}
