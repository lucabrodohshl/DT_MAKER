# Twin designs and running twins

A **twin design** is the versioned definition of a type of system. It contains the editable model plus release configuration. A **running twin** is one concrete instance of a published design. You can have many running twins from one published version.

## The Twin designs library

Open **Twin designs** in the top bar. Each card shows its latest draft, published version, release readiness, instance count and last change. The X in the card's upper right opens a confirmation before removing the design from the library. Existing running twins and saved versions continue to resolve it.

Use the shelves to find drafts, published designs, drafts that need verification and recently modified designs. Search by name, identifier or domain.

## Create or reuse a design

**New twin design** offers five starting points:

- **Blank** starts the Structure and Behavior editor with one system type.
- **Template** supplies a domain starting point that you can change.
- **Clone** copies another design version into a new draft.
- **Import bundle** reads an exported design and its formal artefacts.
- **Formal files** imports existing timed automata, ontology and interpretation files.

For a new design, choose **Create twin design** on the identity step. **Advanced editor** opens the section editor for legacy and specialist work. [Create your first twin](tutorial-first-twin.md) follows the normal graphical path.

## Versions and release

A draft can be edited. A published version cannot. Make a new draft to change a published design. The graphical editor saves its canonical design; the server generates the automaton and semantics from it. The advanced area holds release settings, verification, packages and instances. A running twin pins the exact published version it was made from.

The underlying API still calls this object a blueprint. That name remains in routes and exported bundle formats for compatibility.
