# Third-party source

Source ACE builds but does not own. Each tree is imported unmodified, and
that is the point: ACE is meant to run these programs because ACE implements
the interfaces they expect, not because they were cut down until they fitted.
A diff against upstream is therefore a bug report about ACE.

Both trees build entirely out of tree -- every object lands under `build/` --
so nothing here should ever be dirty as a result of building. If `git status`
reports a change under `third_party/`, something wrote where it should not
have. The Makefile cancels Make's implicit `%.c: %.y` and `%.c: %.l` rules for
exactly this reason: left alone they regenerate `regina/yaccsrc.c` over the
checked-in copy, in place.

## regina/

Regina 3.5, the Rexx interpreter, from the AROS contrib tree.

* Upstream: `https://github.com/aros-development-team/contrib`, `regina/`
* Commit: `ec3f6b50cd9af84ea6bd3e581d93d0e874a6affb`
* Licence: LGPL, see `regina/COPYING-LIB`

Only the `rexx` target is built -- the standalone interpreter. Not the
`regina` shared library, and not `os_other.c`, `mt_amigalib.c` or a Unix
`RexxMast`: the whole exercise is that the Amiga sources run against ACE.
See `docs/regina-amiga-port.md` and `docs/regina-arexx-plan.md`.

To refresh, build against a working checkout rather than editing in place:

```sh
make regina REGINA_SRC=/path/to/aros-contrib/regina
```

## vim/

Vim, built with ACE's Amiga backend seams by `tools/build-vim-ace.sh`.

* Upstream: `https://github.com/vim/vim`
* Commit: `5d41506` ("runtime(sh): Selectively suppress matching syntax errors")
* Licence: Vim licence (charityware), see `vim/LICENSE`

`runtime/` is imported along with `src/`, because `make install-vim` installs
it beside the binary and Vim will not start usefully without it.

To build against a different checkout:

```sh
make vim VIM_SRC=/path/to/vim
```

## LhA

Not vendored: upstream ships release tarballs rather than a tree ACE tracks,
so `make lha` fetches and checksums one into `build/`. See `LHA_AROS_URL` and
`LHA_AROS_SHA256` in the Makefile.
