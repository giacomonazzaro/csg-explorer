template <typename Type>
struct maybe {
  union {
    Type        data;
    std::string message;
  };
  bool ok;

  maybe(Type&& d) : data(d), ok(true) {}
  maybe(const string&& m) : data(d), ok(false) {}

  operator data() const {
    assert(ok);
    return data;
  }

  operator data*() const {
    if (ok)
      return &data;
    else
      return nullptr;
  }
}
