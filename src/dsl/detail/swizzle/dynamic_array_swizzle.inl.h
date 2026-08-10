[[nodiscard]] DynamicArray<T> x() const { OC_ASSERT(size_ > 0); return DynamicArray<T>::create(at(0)); }
[[nodiscard]] DynamicArray<T> y() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1)); }
[[nodiscard]] DynamicArray<T> z() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2)); }
[[nodiscard]] DynamicArray<T> w() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3)); }

[[nodiscard]] DynamicArray<T> xx() const { OC_ASSERT(size_ > 0); return DynamicArray<T>::create(at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3)); }
[[nodiscard]] DynamicArray<T> yx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0)); }
[[nodiscard]] DynamicArray<T> yy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1)); }
[[nodiscard]] DynamicArray<T> yz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2)); }
[[nodiscard]] DynamicArray<T> yw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2)); }
[[nodiscard]] DynamicArray<T> ww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3)); }

[[nodiscard]] DynamicArray<T> xxx() const { OC_ASSERT(size_ > 0); return DynamicArray<T>::create(at(0), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> xyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> xyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> xyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> xyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> xzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> xzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> xzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> xzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> xwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> xwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> xwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> xww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> yxx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> yxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> yxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> yxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> yyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> yyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> yyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> yyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> yzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> yzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> yzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> yzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> ywx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> ywy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> ywz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> yww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> zxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> zxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> zxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> zxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> zyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> zyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> zyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> zyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> zwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> zwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> zwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> zww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> wxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> wxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> wxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> wxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> wyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> wyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> wyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> wyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> wzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> wzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> wzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> wzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> www() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(3)); }

[[nodiscard]] DynamicArray<T> xxxx() const { OC_ASSERT(size_ > 0); return DynamicArray<T>::create(at(0), at(0), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xxxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(0), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xxxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xxxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> xxyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(0), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> xxyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(0), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> xxyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> xxyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> xxzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> xxzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> xxzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(0), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> xxzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> xxwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> xxwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> xxwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> xxww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(0), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> xyxx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xyxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xyxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xyxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> xyyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> xyyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(0), at(1), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> xyyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> xyyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> xyzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> xyzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> xyzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(1), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> xyzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> xywx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> xywy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> xywz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> xyww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(1), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> xzxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xzxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xzxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xzxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> xzyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> xzyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> xzyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> xzyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> xzzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> xzzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> xzzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(0), at(2), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> xzzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> xzwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> xzwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> xzwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> xzww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(2), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> xwxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> xwxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> xwxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> xwxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> xwyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> xwyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> xwyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> xwyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> xwzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> xwzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> xwzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> xwzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> xwwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> xwwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> xwwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> xwww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(0), at(3), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> yxxx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> yxxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> yxxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> yxxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> yxyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> yxyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(0), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> yxyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> yxyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> yxzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> yxzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> yxzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(0), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> yxzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> yxwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> yxwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> yxwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> yxww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(0), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> yyxx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> yyxy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> yyxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> yyxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> yyyx() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> yyyy() const { OC_ASSERT(size_ > 1); return DynamicArray<T>::create(at(1), at(1), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> yyyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> yyyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> yyzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> yyzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> yyzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(1), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> yyzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> yywx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> yywy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> yywz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> yyww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(1), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> yzxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> yzxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> yzxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> yzxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> yzyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> yzyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> yzyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> yzyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> yzzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> yzzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> yzzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(1), at(2), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> yzzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> yzwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> yzwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> yzwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> yzww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(2), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> ywxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> ywxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> ywxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> ywxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> ywyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> ywyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> ywyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> ywyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> ywzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> ywzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> ywzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> ywzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> ywwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> ywwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> ywwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> ywww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(1), at(3), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> zxxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> zxxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> zxxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> zxxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> zxyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> zxyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> zxyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> zxyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zxzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zxzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zxzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(0), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zxzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> zxwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> zxwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> zxwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> zxww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(0), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> zyxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> zyxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> zyxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> zyxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> zyyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> zyyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> zyyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> zyyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zyzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zyzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zyzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(1), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zyzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> zywx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> zywy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> zywz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> zyww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(1), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> zzxx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> zzxy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> zzxz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> zzxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> zzyx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> zzyy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> zzyz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> zzyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zzzx() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zzzy() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zzzz() const { OC_ASSERT(size_ > 2); return DynamicArray<T>::create(at(2), at(2), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zzzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> zzwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> zzwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> zzwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> zzww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(2), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> zwxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> zwxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> zwxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> zwxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> zwyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> zwyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> zwyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> zwyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> zwzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> zwzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> zwzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> zwzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> zwwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> zwwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> zwwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> zwww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(2), at(3), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> wxxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> wxxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> wxxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> wxxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> wxyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> wxyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> wxyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> wxyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> wxzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> wxzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> wxzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> wxzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wxwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wxwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wxwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> wxww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(0), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> wyxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> wyxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> wyxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> wyxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> wyyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> wyyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> wyyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> wyyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> wyzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> wyzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> wyzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> wyzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wywx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wywy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wywz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> wyww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(1), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> wzxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> wzxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> wzxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> wzxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> wzyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> wzyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> wzyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> wzyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> wzzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> wzzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> wzzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> wzzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wzwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wzwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wzwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> wzww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(2), at(3), at(3)); }
[[nodiscard]] DynamicArray<T> wwxx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(0), at(0)); }
[[nodiscard]] DynamicArray<T> wwxy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(0), at(1)); }
[[nodiscard]] DynamicArray<T> wwxz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(0), at(2)); }
[[nodiscard]] DynamicArray<T> wwxw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(0), at(3)); }
[[nodiscard]] DynamicArray<T> wwyx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(1), at(0)); }
[[nodiscard]] DynamicArray<T> wwyy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(1), at(1)); }
[[nodiscard]] DynamicArray<T> wwyz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(1), at(2)); }
[[nodiscard]] DynamicArray<T> wwyw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(1), at(3)); }
[[nodiscard]] DynamicArray<T> wwzx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(2), at(0)); }
[[nodiscard]] DynamicArray<T> wwzy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(2), at(1)); }
[[nodiscard]] DynamicArray<T> wwzz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(2), at(2)); }
[[nodiscard]] DynamicArray<T> wwzw() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(2), at(3)); }
[[nodiscard]] DynamicArray<T> wwwx() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(3), at(0)); }
[[nodiscard]] DynamicArray<T> wwwy() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(3), at(1)); }
[[nodiscard]] DynamicArray<T> wwwz() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(3), at(2)); }
[[nodiscard]] DynamicArray<T> wwww() const { OC_ASSERT(size_ > 3); return DynamicArray<T>::create(at(3), at(3), at(3), at(3)); }
