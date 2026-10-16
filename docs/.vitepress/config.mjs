import { defineConfig } from 'vitepress'

const base = process.env.DOCS_BASE || '/docs/'
const siteUrl = process.env.DOCS_SITE_URL?.replace(/\/$/, '')
const github = 'https://github.com/lucabrodohshl/DT_MAKER'

export default defineConfig({
  title: 'Doppelganger',
  description: 'Design, verify, deploy and operate digital twins.',
  lang: 'en-US',
  base,
  cleanUrls: false,
  rewrites(id) {
    if (id === 'studio/README.md') return 'manual.md'
    return id.startsWith('studio/') ? id.slice('studio/'.length) : id
  },
  srcExclude: ['superpowers/**', 'mainpage.md'],
  sitemap: siteUrl ? { hostname: `${siteUrl}${base}` } : undefined,
  head: [
    ['meta', { name: 'theme-color', content: '#112b3c' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Doppelganger documentation' }],
    ['meta', { property: 'og:description', content: 'Guides, tutorials and technical reference for Doppelganger.' }],
    ['link', { rel: 'icon', type: 'image/png', href: `${base}brand/doppelganger-light.png`, media: '(prefers-color-scheme: light)' }],
    ['link', { rel: 'icon', type: 'image/png', href: `${base}brand/doppelganger-dark.png`, media: '(prefers-color-scheme: dark)' }],
    ...(siteUrl ? [['meta', { property: 'og:url', content: `${siteUrl}${base}` }]] : [])
  ],
  transformHead({ pageData }) {
    if (!siteUrl) return []
    const path = pageData.relativePath === 'index.md'
      ? ''
      : pageData.relativePath.replace(/^studio\//, '').replace(/\.md$/, '.html')
    return [['link', { rel: 'canonical', href: `${siteUrl}${base}${path}` }]]
  },
  themeConfig: {
    logo: { light: '/brand/doppelganger-light.png', dark: '/brand/doppelganger-dark.png', alt: 'Doppelganger' },
    siteTitle: 'Doppelganger',
    search: { provider: 'local', options: { detailedView: true } },
    nav: [
      { text: 'Get started', link: '/installation.html' },
      { text: 'Tutorials', link: '/tutorial-first-twin.html' },
      { text: 'Reference', link: '/runtime-api.html' },
      { text: 'GitHub', link: github }
    ],
    sidebar: [
      { text: 'Start here', items: [
        { text: 'Overview', link: '/' },
        { text: 'Concepts', link: '/manual.html' },
        { text: 'Installation', link: '/installation.html' },
        { text: 'First launch', link: '/getting-started.html' }
      ] },
      { text: 'Tutorials', items: [
        { text: 'Create your first twin', link: '/tutorial-first-twin.html' },
        { text: 'Connect a real system', link: '/tutorial-connect-real-twin.html' },
        { text: 'Evolve an ontology', link: '/tutorial-evolving-an-ontology.html' }
      ] },
      { text: 'Use Studio', items: [
        { text: 'Twin designs', link: '/blueprints.html' },
        { text: 'Design editor', link: '/blueprint-workspace.html' },
        { text: 'Scenarios and preview', link: '/scenarios-and-preview.html' },
        { text: 'Release and deployment', link: '/blueprint-release.html' },
        { text: 'Running twins', link: '/operations.html' },
        { text: 'Audit and replay', link: '/audit-and-replay.html' }
      ] },
      { text: 'Engineering', items: [
        { text: 'Ontologies', link: '/ontology-management.html' },
        { text: 'Refinement', link: '/refinement.html' },
        { text: 'Impact and staleness', link: '/impact-and-staleness.html' },
        { text: 'Releases and rollback', link: '/release-and-deployment.html' }
      ] },
      { text: 'Reference', items: [
        { text: 'Runtime API', link: '/runtime-api.html' },
        { text: 'Supported models', link: '/supported-model-fragment.html' },
        { text: 'Compiler diagnostics', link: '/compiler-diagnostics.html' },
        { text: 'Logical time', link: '/logical-time-model.html' },
        { text: 'Guarantees and limits', link: '/limitations.html' },
        { text: 'Troubleshooting', link: '/troubleshooting.html' }
      ] },
      { text: 'Develop and deploy', items: [
        { text: 'Architecture', link: '/architecture.html' },
        { text: 'Canonical model', link: '/model-architecture.html' },
        { text: 'Developer guide', link: '/developer.html' },
        { text: 'Domain plugins', link: '/plugins.html' },
        { text: 'Docs hosting', link: '/hosting.html' },
        { text: 'Contributing', link: '/contributing.html' }
      ] }
    ],
    socialLinks: [{ icon: 'github', link: github }],
    outline: { level: [2, 3], label: 'On this page' },
    docFooter: { prev: 'Previous', next: 'Next' },
    returnToTopLabel: 'Back to top',
    darkModeSwitchLabel: 'Theme',
    sidebarMenuLabel: 'Contents'
  }
})
