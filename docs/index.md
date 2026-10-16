---
layout: home
title: Doppelganger documentation

hero:
  name: Doppelganger
  text: Build a twin you can explain.
  tagline: Practical guides for designing, verifying, deploying and operating digital twins.
  image:
    light: /brand/doppelganger-light.png
    dark: /brand/doppelganger-dark.png
    alt: Doppelganger mark
  actions:
    - theme: brand
      text: Install and start
      link: /installation.html
    - theme: alt
      text: Create your first twin
      link: /tutorial-first-twin.html

features:
  - title: Design
    details: Describe structure and behavior once, then inspect the generated automaton, ontology, knowledge graph and contract.
    link: /blueprint-workspace.html
  - title: Verify
    details: Validate models, review alignment and release only with evidence tied to the saved version.
    link: /blueprint-release.html
  - title: Operate
    details: Connect an instance, inspect telemetry and conformance, and replay recorded decisions.
    link: /operations.html
---

## Choose your path

- **New to the project?** Read [installation](installation.md), then follow [create your first twin](tutorial-first-twin.md).
- **Using an existing twin?** Start with [running twins](operations.md) and [connect a real system](tutorial-connect-real-twin.md).
- **Extending the code?** Read the [architecture](architecture.md), [developer guide](developer.md), and [contribution guide](contributing.md).

![Doppelganger showing the drone twin and its map](./screenshots/doppelganger-drone-hero.png)

<p class="screenshot-caption">The shipped drone example in Doppelganger. The physical simulation and the twin's known map are shown side by side.</p>

The documentation is a static site. Search, images, and page navigation work without the application, database or any runtime service.
