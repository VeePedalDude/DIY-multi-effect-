# tests/models

`.nam` captures covering every architecture NAM core plays, used by `tests/native/test_nam.cpp`,
the parity check and the browser tests.

| File | Architecture | From |
|---|---|---|
| `wavenet_a1_standard.nam` | WaveNet, A1 "standard" (no sample rate or loudness in the file) | NeuralAmpModelerCore `example_models/` (MIT) |
| `A2.nam` | SlimmableContainer of A2 WaveNets | 〃 |
| `wavenet_a2_feature_test.nam` | WaveNet with A2 features | 〃 |
| `slimmable_wavenet.nam`, `slimmable_container.nam` | slimmable WaveNet, SlimmableContainer | 〃 |
| `wavenet.nam`, `wavenet_condition_dsp.nam` | small WaveNet, WaveNet with a condition DSP | 〃 |
| `lstm.nam` | LSTM | 〃 |
| `convnet.nam`, `linear.nam`, `sequential.nam` | ConvNet, Linear, Sequential (ConvNet + Linear) | written by `make_models.py` (seeded random weights; they only need to load and play) |

These are test files, not tones to play through. Real captures: [TONE3000](https://www.tone3000.com).
