# AI libraries

Five libraries for data and machine learning, written entirely in GlowLang.
They live in `lib/`, so a program loads them by name:

```glow
use "glownum"      # numbers, vectors, matrices   (like NumPy)
use "glowframe"    # tables of data and CSV       (like pandas)
use "glowlearn"    # classic machine learning     (like scikit-learn)
use "glowgrad"     # autograd and neural networks (like PyTorch)
use "glowtext"     # text for machine learning
```

Rules they all follow:

- A vector is a list of numbers. A matrix is a list of rows. Results are
  plain lists, so every library accepts what the others return.
- Nothing is changed in place. Functions return new values.
- Anything random takes a seed. The same seed gives the same result on
  every machine.
- A mistake (shapes that do not match, a model used before `fit`, a
  missing column) raises an error that starts with the library's name,
  such as `glownum: shapes (2) and (3) do not match`.

Each library has a test file in `tests/` (`test_glownum.glow` and so on)
that also serves as a set of worked examples.

## glownum

Everything is reached through the global `gnum`.

```glow
use "glownum"
let a = [[4, 3], [6, 3]]
show gnum.dot(a, gnum.inv(a))           # the identity matrix
show gnum.mean(a, 0)                    # column means: [5, 3]
show gnum.solve([[2, 1], [1, 3]], [3, 5])
let rng = RandomState(42)
show rng.randn([2, 3])                  # 2 x 3 normal numbers
```

| Group | Functions |
|---|---|
| Making arrays | `zeros`, `ones`, `full`, `eye`, `arange`, `linspace`, `copy` (a shape is a length or `[rows, cols]`) |
| Shape | `shape`, `ndim`, `size`, `flatten`, `reshape`, `transpose`, `row`, `column`, `concat` |
| Arithmetic | `add`, `sub`, `mul`, `div`, `pow`, `minimum`, `maximum`, `neg`, `abs`, `sqrt`, `exp`, `log`, `square`, `sign`, `floor`, `ceil`, `round`, `clip`, `apply`, `sigmoid`, `tanh`, `relu`, `softmax` |
| Reductions | `sum`, `mean`, `min`, `max`, `argmin`, `argmax`, `prod`, `var`, `std`, `median`, `percentile`, `cumsum`, `diff` |
| Order | `sort`, `argsort` (stable), `unique`, `take` |
| Linear algebra | `dot`, `matmul`, `outer`, `norm`, `trace`, `det`, `inv`, `solve`, `lstsq`, `eigh`, `cholesky` |
| Statistics | `cov`, `corrcoef`, `allclose` |
| Random | `RandomState(seed)`: `random`, `uniform`, `randint`, `normal`, `rand`, `randn`, `permutation`, `shuffled`, `choice` |

Arithmetic pairs a number with anything, and a vector with each row of a
matrix. Reductions take an optional axis: `none` for everything, `0` for
one result per column, `1` for one per row.

## glowframe

```glow
use "glowframe"
let df = read_csv("people.csv")
let adults = df.filter(lambda(row): row["age"] != none and row["age"] >= 18)
show adults.group_by("team").agg({"score": ["mean", "max"]}).to_string()
let X = adults.dropna().to_matrix(["age", "score"])
```

A `DataFrame` is a set of named columns. A missing value is `none`, and
statistics skip it.

| Group | Functions and methods |
|---|---|
| Building | `DataFrame({"name": [...]})`, `frame_from_rows`, `frame_from_matrix`, `frame_concat`, `read_csv`, `parse_csv` |
| Looking | `shape`, `columns`, `count_rows`, `col`, `row`, `rows`, `head`, `tail`, `take`, `sample`, `to_string` |
| Columns | `select`, `drop`, `rename`, `with_column`, `map_column` |
| Rows | `filter`, `sort_values`, `dropna`, `fillna`, `drop_duplicates` |
| Statistics | `sum`, `mean`, `min`, `max`, `median`, `std`, `count`, `corr`, `unique`, `value_counts`, `describe`, `isna_counts`, `numeric_columns` |
| Groups | `group_by(names).agg({...})`, `.size()`, `.parts()` |
| Joins | `merge(other, on, how)` with `"inner"` or `"left"` |
| Output | `to_matrix`, `to_csv`, `to_csv_text` |

Aggregations: `sum`, `mean`, `min`, `max`, `count`, `std`, `median`,
`first`, `last`, `nunique`. CSV reading handles quoted cells, doubled
quotes, separators and new lines inside quotes, and turns cells into
numbers, booleans, text or `none` (an empty cell).

## glowlearn

```glow
use "glowlearn"
let parts = train_test_split(X, y, 0.25, 42)     # [X_train, X_test, y_train, y_test]
let model = RandomForestClassifier(30).fit(parts[0], parts[2])
show accuracy_score(parts[3], model.predict(parts[1]))
```

`X` is a matrix with one row per sample. `y` has one entry per sample:
numbers for regression, numbers or strings for classification. Every model
has `fit` (which returns the model), `predict` and `score`; classifiers
also have `predict_proba` and `classes`.

| Group | Names |
|---|---|
| Splitting | `train_test_split`, `KFold`, `cross_val_score` |
| Preparing | `StandardScaler`, `MinMaxScaler`, `LabelEncoder`, `OneHotEncoder` |
| Regression | `LinearRegression`, `Ridge`, `KNeighborsRegressor`, `DecisionTreeRegressor`, `RandomForestRegressor` |
| Classification | `LogisticRegression`, `KNeighborsClassifier`, `GaussianNB`, `DecisionTreeClassifier`, `RandomForestClassifier` |
| Clustering | `KMeans` |
| Fewer dimensions | `PCA` |
| Metrics | `accuracy_score`, `confusion_matrix`, `precision_score`, `recall_score`, `f1_score`, `mean_squared_error`, `root_mean_squared_error`, `mean_absolute_error`, `r2_score` |

## glowgrad

```glow
use "glowgrad"
let model = Sequential([Dense(2, 8, 1), Activation("tanh"), Dense(8, 1, 2)])
let opt = AdamOptimizer(model.parameters(), 0.05)
let losses = train_network(model, X, y, opt, "bce", 400)
show model.predict([[0, 1]])
model.save("model.json")
```

A `Var` holds a matrix and remembers how it was computed. Calling
`backward()` on a single-number result fills in `.grad` for every `Var`
created with `requires_grad = true`.

```glow
let w = Var(3, true)
let loss = w.mul(w).add(w)      # w^2 + w
loss.backward()
show w.grad                     # [[7]]
```

| Group | Names |
|---|---|
| `Var` operations | `add`, `sub`, `mul`, `div`, `matmul`, `neg`, `relu`, `tanh`, `sigmoid`, `exp`, `log`, `pow`, `sum`, `mean`, `transpose`, `backward`, `zero_grad`, `item`, `tolist`, `shape` |
| Losses | `mse_loss`, `cross_entropy_loss`, `bce_loss` (the last two take raw scores, not probabilities) |
| Layers | `Dense`, `Activation("relu" / "tanh" / "sigmoid")`, `Dropout`, `Sequential` |
| Optimizers | `SGDOptimizer` (with momentum), `AdamOptimizer` |
| Training | `train_network(model, X, y, optimizer, loss, epochs, batch_size, seed)` |
| Saving | `model.save(path)`, `model.load(path)`, `model.state()`, `model.load_state(state)` |

## glowtext

```glow
use "glowtext"
let clf = TextClassifier().fit(["great film", "awful film"], ["pos", "neg"])
show clf.predict(["a great one"])                  # [pos]

let tfidf = TfidfVectorizer()
let rows = tfidf.fit_transform(documents)
show most_similar(tfidf.transform(["query words"])[0], rows, 3)
```

| Group | Names |
|---|---|
| Tokens | `tokenize`, `sentences`, `stop_words`, `remove_stop_words`, `ngrams`, `char_ngrams`, `word_counts`, `most_common` |
| Distances | `edit_distance`, `jaccard_similarity`, `cosine_similarity`, `most_similar` |
| Vectorizers | `CountVectorizer`, `TfidfVectorizer` (`fit`, `transform`, `fit_transform`, `vocabulary`) |
| Classifiers | `MultinomialNB`, `TextClassifier` |

## Speed and limits

These libraries run on the GlowLang VM and JIT, with no native code of
their own. They suit learning, small and medium data sets (thousands of
rows, tens of features) and small networks. They are not a replacement for
GPU frameworks on large models.

The older `lib/ai.glow`, `lib/glowtorch*.glow`, `lib/ml_pipeline.glow`,
`lib/dataset.glow` and `lib/dataframe.glow` are unchanged. Some names
overlap (`StandardScaler`, `OneHotEncoder`), so load either the old
libraries or these in one program, not both.
