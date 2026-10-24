# Demo images and recordings

## Four fields, one workspace

The current README uses the same independent examples as [texline.dev](https://texline.dev/). These are captures from the running Windows application, not recreated interface mockups. Recordings use scripted input on an isolated, inactive desktop with private settings. They are demonstrations, not physical-input tests or compilation-speed benchmarks.

| File in `showcase/` | What it shows | Resolution |
| :--- | :--- | :--- |
| `archaeology.png` | Source and compiled object-study page | 2560 × 1452 |
| `ecology.gif` | Section editing, entering focus mode, continuing to write | 1440 × 630 |
| `ecology.png` | Static focus view for reduced motion | 2560 × 1120 |
| `mathematics.png` | A spectral bound pinned beneath its proof | 2560 × 1452 |
| `physics.gif` | Reordering a Beamer frame, progressive reveals, expanded Saturn preview | 1440 × 817 |
| `physics.png` | Static expanded preview for reduced motion | 2560 × 1452 |

PNGs are original 2× native captures. GIFs retain the recorded timing, play once and finish on the writing or expanded-preview view. GitHub controls when its images load; the website provides replayable high-definition video. The README supplies static images when the browser requests reduced motion. No application UI was generated or retouched.

The slide reorder recording drives TeXline’s native drag card, insertion animation and guarded source-editing callback. It bypasses Windows OLE drag dispatch on the inactive desktop, so it does not establish physical-mouse behavior.

### Sources and credits

- **Archaeology.** [Terracotta amphora (jar)](https://www.metmuseum.org/art/collection/search/252496), attributed to the Acheloös Painter, Greek, Attic, late sixth century BCE. The Metropolitan Museum of Art, accession 26.60.29. Fletcher Fund, 1926. Image: public domain / CC0 through the Met’s [Open Access program](https://www.metmuseum.org/policies/image-resources).
- **Ecology.** The original explanatory note draws on the abstract of [Marthews et al. (2008), *Modelling direct radiation and canopy gap regimes in tropical forests*](https://repository.si.edu/items/f878d2df-24cc-41fa-8e4a-5d7e10f10d63). It does not report new field measurements. The editor uses its existing Georgia font setting.
- **Mathematics.** The sample text, proof and diagram were prepared for this demonstration.
- **Physics.** [A Splendor Seldom Seen](https://science.nasa.gov/photojournal/a-splendor-seldom-seen/), Cassini, 17 October 2012, PIA14934 (enhanced color). Credit: NASA/JPL-Caltech/Space Science Institute. Used under the [JPL Image Use Policy](https://www.jpl.nasa.gov/jpl-image-use-policy/); no endorsement is implied. The orbital example omits ring-particle interactions and other perturbations.

[Watch the videos and read the credits](https://texline.dev/media.html) · [Download all four sample projects](https://texline.dev/samples/showcase-samples.zip)

The samples’ page designs are not built-in TeXline templates. TeXline UI copyrights and license notices continue to apply; third-party images retain the terms above.

## Earlier publication media

The following records describe the earlier rank-one and visual-editor samples, retained for existing links and release history.

The PNGs and slide GIF are native Qt window captures made on Windows on 24 October 2026, using an inactive private desktop, private settings and independent examples. The capture harness links the same application objects as the published 1.0 executable (SHA-256 `791c526c4a7d433f95d374a49eb20a489d4438b4b96de6d6e97c01d9d78e3d50`). No product UI was generated or retouched.

The static screenshots were captured at an effective device pixel ratio of 2 and saved as lossless PNGs, without enlarging smaller images. The reading and slide images are 2360 × 1452 pixels. The page uses a single window as its opening image to keep text legible; original files remain available at full resolution. The compact GIF is a separate, lower-resolution recording and is not the primary screenshot.

- `writing.png`: two complete application windows arranged side by side, with unchanged pixels and a small publication margin.
- `reading.png`: a compiled theorem pinned below a section of its source.
- `figures.png`: actual multi-image layout preview. The chart curves are illustrative functions created for the demonstration, not experimental measurements.
- `formula.png`: the real formula editor and its compiled matrix preview.
- `steps.png`: the progressive reveal editor on a three-step Beamer proof.
- `slides.png` and `slides.gif`: real Beamer navigation and preview expansion. The GIF uses the recorded frame intervals rounded to 10 ms and is not sped up. Input was scripted; it is not a recording of physical input or a display-compositor performance benchmark.
- `wordmark.svg`: original vector typography for this publication page.

The note and talk are in [examples/showcase](../../examples/showcase). The existing bundled version demonstration is retained in the repository; its sources and terms are recorded in [onboarding sources](../../res/research/onboarding/sources.md).

TeXline UI copyrights and the project's license notices continue to apply. The new independent example documents are CC0-1.0. No third-party product artwork, screenshots or website code was copied for this page.
