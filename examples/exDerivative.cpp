#include <LatAnalyze/Numerical/Derivative.hpp>
#include <LatAnalyze/Functional/CompiledFunction.hpp>
#include <LatAnalyze/Core/Math.hpp>

using namespace std;
using namespace Latan;

int main(int argc, char *argv[])
{
    string source;
    Index  maxOrder;
    double x;
    
    if (argc != 4)
    {
        cerr << "usage: " << argv[0] << " <function> <max order> <point>";
        cerr << endl;
        
        return EXIT_FAILURE;
    }
    source   = argv[1];
    maxOrder = strTo<Index>(argv[2]);
    x        = strTo<double>(argv[3]);
    
    DoubleFunction    f = compile(source, 1);
    CentralDerivative df(f);
    
    for (Index i = 1; i <= 4; ++i)
    {
        cout << "--- O(h^" << 2*i << ") derivative" << endl;
        for (Index j = 0; j <= maxOrder; ++j)
        {
            df.setOrder(j, i);
            cout << "d^" << j << "f(" << x << ")= " << df(&x) << endl;
        }
    }
    
    return 0;
}
