#define GLOBAL_NAMESPACE_NAME XTCP
#include <vector>
namespace GLOBAL_NAMESPACE_NAME
{
    template <class T>
    class EventBundle
    {
    private:
        std::vector<T> handlers;

    public:
        void subscribe(T handler)
        {
            handlers.push_back(handler);
        };
        template <class... ARG>
        void operator()(ARG... args)
        {
            for (auto h : handlers)
            {
                h(args...);
            }
        };
    };
}