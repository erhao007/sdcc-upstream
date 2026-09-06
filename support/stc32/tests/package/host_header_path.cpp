// Project-owned regression: Boost's enabled assertion expands __FILE__ in a
// system header. It remains in PE .rdata after debug sections are stripped.
#include <boost/shared_ptr.hpp>

int main(int argc, char **)
{
    boost::shared_ptr<int> value;
    if (argc == 1)
        value.reset(new int(0));
    return *value;
}
