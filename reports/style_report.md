# Initial Style Report: MarveIous

This report is descriptive, not yet a trained imitation model.

## Dataset

- Games represented: **509**
- Personal move decisions: **14,740**
- Train / validation / test decisions: **9,902 / 2,426 / 2,412**
- Recorded rating range: **586 to 1638**
- Average recorded rating: **1511.4**

## Move Tendencies

- Capture rate: **24.75%**
- Checking-move rate: **10.52%**
- Castles observed: **290**
- Kingside / queenside castles: **160 / 129**

## Most Common ECO Codes

| ECO | Decisions |
| --- | ---: |
| C42 | 1,886 |
| C50 | 1,848 |
| A40 | 1,169 |
| B01 | 699 |
| C20 | 675 |
| C41 | 633 |
| C51 | 616 |
| C57 | 437 |
| C24 | 375 |
| B00 | 362 |
| A00 | 358 |
| B30 | 333 |
| C58 | 331 |
| C00 | 319 |
| B13 | 314 |

## Next Modeling Step

Train a candidate-move reranker on the train split, tune its style weight on validation, and report top-1/top-3 move matching on the untouched test split.
