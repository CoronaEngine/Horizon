//
// Created by Mike Smith on 2021/12/24.
//


namespace eastl
{
	inline allocator::allocator(const char* EASTL_NAME(pName))
	{
#if EASTL_NAME_ENABLED
		mpName = pName ? pName : EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline allocator::allocator(const allocator& EASTL_NAME(alloc))
	{
#if EASTL_NAME_ENABLED
		mpName = alloc.mpName;
#endif
	}


	inline allocator::allocator(const allocator&, const char* EASTL_NAME(pName))
	{
#if EASTL_NAME_ENABLED
		mpName = pName ? pName : EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline allocator& allocator::operator=(const allocator& EASTL_NAME(alloc))
	{
#if EASTL_NAME_ENABLED
		mpName = alloc.mpName;
#endif
		return *this;
	}


	inline const char* allocator::get_name() const
	{
#if EASTL_NAME_ENABLED
		return mpName;
#else
		return EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
	}


	inline void allocator::set_name(const char* EASTL_NAME(pName))
	{
#if EASTL_NAME_ENABLED
		mpName = pName;
#endif
	}


	inline bool operator==(const allocator&, const allocator&)
	{
		return true; // All allocators are considered equal, as they merely use global new/delete.
	}


	inline bool operator!=(const allocator&, const allocator&)
	{
		return false; // All allocators are considered equal, as they merely use global new/delete.
	}


} // namespace eastl
