# Awase

**合わせ** *(awase)*: bringing two things together to see whether they match.

Awase is a proposed app for volunteer package builds for [Renku OS](https://renkuos.org/). Volunteers lend their computers' idle time to build [RenkuOSPorts](https://github.com/RenkuOS/RenkuOSPorts) packages, like SETI@home or a mining pool. Every package is built twice, on two volunteers' machines, and ships only when both builds match.

**Website:** https://idumych.github.io/awase/
**Proposal:** [PROPOSAL.md](PROPOSAL.md)

Nothing is running yet. This repository holds the proposal and its website, for the Renku OS contributors to discuss.

## The site

Plain static HTML, CSS and JavaScript with no build step, in the style of the [Renku OS website draft](https://github.com/idumych/renkuos-website-draft). Open `index.html` or serve the folder.

```
index.html          the page
assets/style.css    all styles
assets/pool.js      the hero animation (packages sealed when two builds match) and the menu
assets/awase.svg    the seal, also the favicon
assets/logo.png     the Renku OS wordmark
PROPOSAL.md         the full proposal
```

## Licence

MIT. See [LICENSE](LICENSE).
