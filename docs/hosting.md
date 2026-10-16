# Build and host the documentation

The documentation is a separate VitePress package in `docs/`. Its pages, local search index, scripts and images are emitted as static files. The application, database, backend and runtime processes are unnecessary for either the docs build or the deployed site.

## Run locally

```bash
cd docs
npm ci
npm run dev
```

Open the printed local URL. The default base path is `/docs/` so the built site can also be bundled under the application's Help route. To serve it at the root of a standalone domain during development, run `DOCS_BASE=/ npm run dev`.

## Build and preview the static output

```bash
cd docs
DOCS_BASE=/ npm run check
DOCS_BASE=/ npm run preview
```

Upload the contents of `docs/.vitepress/dist/` to a static host. The preview command serves that output locally. Every guide, image, search index, navigation route and script is included in the directory.

## Configure a public URL

Set `DOCS_BASE` to the path where the site will live, always with leading and trailing slashes. Set `DOCS_SITE_URL` to the origin, without a trailing slash, to generate a sitemap, canonical links and social URL metadata.

| Hosting target | Example build settings |
|---|---|
| Domain root | `DOCS_BASE=/ DOCS_SITE_URL=https://docs.example.org npm run build` |
| GitHub Pages project path | `DOCS_BASE=/DT_MAKER/ DOCS_SITE_URL=https://username.github.io npm run build` |
| Cloudflare Pages, Netlify or Vercel | Root directory `docs`, build command `npm ci && npm run build`, output directory `.vitepress/dist` |

For GitHub Pages, configure the deployment workflow to publish `docs/.vitepress/dist/` after building with the repository path as `DOCS_BASE`. The site leaves `.html` in page URLs, so a static host does not need rewrite rules for deep links. Set the Pages publishing mode to GitHub Actions before enabling such a workflow. No hosting credentials belong in this repository.

The tracked `robots.txt` permits indexing. Before public launch, choose the final URL and build with `DOCS_SITE_URL` so the sitemap and canonical metadata use that address. This repository currently has no root license; decide licensing terms before inviting outside contributions.
