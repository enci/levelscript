// E118: a use path the resolver cannot map (§7.3 #9).
// @expect error
// @expect stderr-contains cannot resolve module "no_such_module.ls"
use "no_such_module.ls"
sequence main { }
