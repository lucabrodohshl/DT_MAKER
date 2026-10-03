# Third-Party Dependencies

This artifact's own source code is released under the MIT License (see
`LICENSE`). It depends on the following external components, which are **not
redistributed in this repository**. `setup.sh` fetches and builds the two
UPPAAL libraries; Z3 is installed from the system package manager; and
nlohmann/json is fetched by CMake (or taken from the system if present).

| Component | Version | License | Copyright | How obtained |
|-----------|---------|---------|-----------|--------------|
| UPPAAL DBM library (UDBM) | upstream `master` | GPL-3.0 | © 1995–2003 Uppsala University and Aalborg University | `setup.sh` clones `github.com/UPPAALModelChecker/UDBM` |
| UPPAAL timed-automata parser (UTAP) | upstream `master` | LGPL-2.1-or-later | © 2002–2006 Uppsala University and Aalborg University (G. Behrmann, M. Mikucionis, U. Larsen) | `setup.sh` clones `github.com/UPPAALModelChecker/utap` |
| Z3 SMT solver | system `libz3-dev` | MIT | © Microsoft Corporation | apt: `libz3-dev` |
| nlohmann/json | v3.11.3 | MIT | © 2013–2022 Niels Lohmann | CMake `FetchContent` (or system package) |

## License obligations

- **UDBM (GPL-3.0):** statically linked into the benchmark binaries. Any
  redistributed binary built from this source is a combined work governed by
  GPL-3.0; distribute the corresponding source and a copy of the GPL-3.0 text
  with such binaries. This artifact ships **source only**, so no GPL-covered
  binary is redistributed here.
- **UTAP (LGPL-2.1):** keep the library replaceable/relinkable in any
  distributed binary, and retain its copyright and license text.
- **Z3, nlohmann/json (MIT):** retain the copyright and permission notice.

The full license texts are available in each project's repository and, after
running `setup.sh`, under `UDBM/LICENSE` and `utap/LICENSE`.
