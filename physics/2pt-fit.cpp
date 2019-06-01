#include <LatAnalyze/Core/OptParser.hpp>
#include <LatAnalyze/Functional/CompiledModel.hpp>
#include <LatAnalyze/Io/Io.hpp>
#include <LatAnalyze/Statistics/MatSample.hpp>
#include <LatAnalyze/Core/Math.hpp>
#include <LatAnalyze/Numerical/MinuitMinimizer.hpp>
#include <LatAnalyze/Numerical/NloptMinimizer.hpp>
#include <LatAnalyze/Core/Plot.hpp>
#include <LatAnalyze/Statistics/XYSampleData.hpp>

using namespace std;
using namespace Latan;

int main(int argc, char *argv[])
{
    // parse arguments /////////////////////////////////////////////////////////
    OptParser            opt;
    bool                 parsed, doPlot, doHeatmap, doCorr, fold;
    string               corrFileName, model, outFileName, outFmt;
    Index                ti, tf, shift, nPar, thinning;
    double               svdTol,plotrange;
    Minimizer::Verbosity verbosity;
    
    opt.addOption("" , "ti"       , OptParser::OptType::value  , false,
                  "initial fit time");
    opt.addOption("" , "tf"       , OptParser::OptType::value  , false,
                  "final fit time");
    opt.addOption("t" , "thinning", OptParser::OptType::value  , true,
                  "thinning of the time interval", "1");
    opt.addOption("s", "shift"    , OptParser::OptType::value  , true,
                  "time variable shift", "0");
    opt.addOption("m", "model"    , OptParser::OptType::value  , true,
                  "fit model (exp|exp2|exp3|cosh|cosh2|cosh3|explin|<interpreter code>)", "cosh");
    opt.addOption("" , "nPar"     , OptParser::OptType::value  , true,
                  "number of model parameters for custom models "
                  "(-1 if irrelevant)", "-1");
    opt.addOption("" , "svd"      , OptParser::OptType::value  , true,
                  "singular value elimination threshold", "0.");
    opt.addOption("v", "verbosity", OptParser::OptType::value  , true,
                  "minimizer verbosity level (0|1|2)", "0");
    opt.addOption("o", "output",    OptParser::OptType::value  , true,
                  "output file", "");
    opt.addOption("" , "uncorr"   , OptParser::OptType::trigger, true,
                  "only do the uncorrelated fit");
    opt.addOption("" , "fold"     , OptParser::OptType::trigger, true,
                  "fold the correlator");
    opt.addOption("p", "plot"     , OptParser::OptType::trigger, true,
                  "show the fit plot");
    opt.addOption("r" , "range",    OptParser::OptType::value  , true,
                  "vertical range multiplier in plots", "20.");
    opt.addOption("h", "heatmap"  , OptParser::OptType::trigger, true,
                  "show the fit correlation heatmap");
    opt.addOption("", "help"      , OptParser::OptType::trigger, true,
                  "show this help message and exit");
    parsed = opt.parse(argc, argv);
    if (!parsed or (opt.getArgs().size() != 1) or opt.gotOption("help"))
    {
        cerr << "usage: " << argv[0] << " <options> <correlator file>" << endl;
        cerr << endl << "Possible options:" << endl << opt << endl;
        
        return EXIT_FAILURE;
    }
    corrFileName = opt.getArgs().front();
    ti           = opt.optionValue<Index>("ti");
    tf           = opt.optionValue<Index>("tf");
    thinning     = opt.optionValue<Index>("t");
    shift        = opt.optionValue<Index>("s");
    model        = opt.optionValue("m");
    nPar         = opt.optionValue<Index>("nPar");
    svdTol       = opt.optionValue<double>("svd");
    outFileName  = opt.optionValue<string>("o");
    doCorr       = !opt.gotOption("uncorr");
    fold         = opt.gotOption("fold");
    doPlot       = opt.gotOption("p");
    plotrange    = opt.optionValue<Index>("range");
    doHeatmap    = opt.gotOption("h");
    switch (opt.optionValue<unsigned int>("v"))
    {
        case 0:
            verbosity = Minimizer::Verbosity::Silent;
            break;
        case 1:
            verbosity = Minimizer::Verbosity::Normal;
            break;
        case 2:
            verbosity = Minimizer::Verbosity::Debug;
            break;
        default:
            cerr << "error: wrong verbosity level" << endl;
            return EXIT_FAILURE;
    }
    
    // load correlator /////////////////////////////////////////////////////////
    DMatSample tmp, corr;
    Index      nSample, nt;
    
    tmp     = Io::load<DMatSample>(corrFileName);
    nSample = tmp.size();
    nt      = tmp[central].rows();
    tmp     = tmp.block(0, 0, nt, 1);
    corr    = tmp;
    FOR_STAT_ARRAY(corr, s)
    {
        for (Index t = 0; t < nt; ++t)
        {
            corr[s]((t - shift + nt)%nt) = tmp[s](t);
        }
    }
    if (fold)
    {
        tmp = corr;
        FOR_STAT_ARRAY(corr, s)
        {
            for (Index t = 0; t < nt; ++t)
            {
                corr[s](t) = 0.5*(tmp[s](t) + tmp[s]((nt - t) % nt));
            }
        }
    }
    
    // make model //////////////////////////////////////////////////////////////
    DoubleModel mod;
    bool        coshModel = false, linearModel = false;
    
    if ((model == "exp") or (model == "exp1"))
    {
        nPar = 2;
        mod.setFunction([](const double *x, const double *p)
                        {
                            return p[1]*exp(-p[0]*x[0]);
                        }, 1, nPar);
    }
    else if (model == "exp2")
    {
        nPar = 4;
        mod.setFunction([](const double *x, const double *p)
                        {
                            return p[1]*exp(-p[0]*x[0]) + p[3]*exp(-p[2]*x[0]);
                        }, 1, nPar);
    }
    else if (model == "exp3")
    {
        nPar = 6;
        mod.setFunction([](const double *x, const double *p)
                        {
                            return p[1]*exp(-p[0]*x[0]) + p[3]*exp(-p[2]*x[0])
                                   + p[5]*exp(-p[4]*x[0]);
                        }, 1, nPar);
    }
    else if ((model == "cosh") or (model == "cosh1"))
    {
        coshModel = true;
        nPar      = 2;
        mod.setFunction([nt](const double *x, const double *p)
                        {
                            return p[1]*(exp(-p[0]*x[0])+exp(-p[0]*(nt-x[0])));
                        }, 1, nPar);
    }
    else if (model == "cosh2")
    {
        coshModel = true;
        nPar      = 4;
        mod.setFunction([nt](const double *x, const double *p)
                        {
                            return p[1]*(exp(-p[0]*x[0])+exp(-p[0]*(nt-x[0])))
                                 + p[3]*(exp(-p[2]*x[0])+exp(-p[2]*(nt-x[0])));
                        }, 1, nPar);
    }
    else if (model == "cosh3")
    {
        coshModel = true;
        nPar      = 6;
        mod.setFunction([nt](const double *x, const double *p)
                        {
                            return p[1]*(exp(-p[0]*x[0])+exp(-p[0]*(nt-x[0])))
                                 + p[3]*(exp(-p[2]*x[0])+exp(-p[2]*(nt-x[0])))
                                 + p[5]*(exp(-p[2]*x[0])+exp(-p[4]*(nt-x[0])));
                        }, 1, nPar);
    }
    else if (model == "explin")
    {
        linearModel = true;
        nPar        = 2;
        mod.setFunction([](const double *x, const double *p)
                        {
                            return p[1] - p[0]*x[0];
                        }, 1, nPar);
    }
    else
    {
        if (nPar > 0)
        {
            mod = compile(model, 1, nPar);
        }
        else
        {
            cerr << "error: please specify the number of model parameter"
                    " using the --nPar function" << endl;
            
            return EXIT_FAILURE;
        }
    }
    
    // fit /////////////////////////////////////////////////////////////////////
    DMatSample          tvec(nSample);
    XYSampleData        data(nSample);
    SampleFitResult     fit;
    DVec                init(nPar);
    NloptMinimizer      globMin(NloptMinimizer::Algorithm::GN_CRS2_LM);
    MinuitMinimizer     locMin;
    vector<Minimizer *> unCorrMin{&globMin, &locMin};

    FOR_STAT_ARRAY(tvec, s)
    {
        tvec[s] = DVec::LinSpaced(nt, 0, nt - 1);
    }
    data.addXDim(nt, "t/a", true);
    data.addYDim("C(t)");
    data.setUnidimData(tvec, corr);
    for (Index p = 0; p < nPar; p += 2)
    {
        mod.parName().setName(p, "E_" + strFrom(p/2));
        mod.parName().setName(p + 1, "Z_" + strFrom(p/2));
    }
    if (linearModel)
    {
        init(0) = data.y(nt/4, 0)[central] - data.y(nt/4 + 1, 0)[central];
        init(1) = data.y(nt/4, 0)[central] + nt/4*init(0);
    }
    else
    {
        init(0) = log(data.y(nt/4, 0)[central]/data.y(nt/4 + 1, 0)[central]);
        init(1) = data.y(nt/4, 0)[central]/(exp(-init(0)*nt/4));
    }
    for (Index p = 2; p < nPar; p += 2)
    {
        init(p)     = 2*init(p - 2);
        init(p + 1) = init(p - 1)/2.;
    }
    for (Index p = 0; p < nPar; p += 2)
    {
        if (linearModel)
        {
            globMin.setLowLimit(p, -10.*fabs(init(p)));
            globMin.setHighLimit(p, 10.*fabs(init(p)));
        }
        else
        {
            globMin.setLowLimit(p, 0.);
            locMin.setLowLimit(p, 0.);
            globMin.setHighLimit(p, 10.*init(p));
        }
        globMin.setLowLimit(p + 1, -10.*fabs(init(p + 1)));
        globMin.setHighLimit(p + 1, 10.*fabs(init(p + 1)));
    }
    globMin.setPrecision(0.001);
    globMin.setMaxIteration(100000);
    globMin.setVerbosity(verbosity);
    locMin.setMaxIteration(1000000);
    locMin.setVerbosity(verbosity);
    for (Index t = 0; t < nt; ++t)
    {
        data.fitPoint((t >= ti) and (t <= tf)
                      and ((t - ti) % thinning == 0), t);
    }
    if (doCorr)
    {
        cout << "-- uncorrelated fit..." << endl;
    }
    cout << "using model '" << model << "'" << endl;
    data.setSvdTolerance(svdTol);
    data.assumeYYCorrelated(false, 0, 0);
    fit = data.fit(unCorrMin, init, mod);
    fit.print();
    if (doCorr)
    {
        cout << "-- correlated fit..." << endl;
        cout << "using model '" << model << "'" << endl;
        init = fit[central];
        data.assumeYYCorrelated(true, 0, 0);
        fit = data.fit(locMin, init, mod);
        fit.print();
    }
    
    // plots ///////////////////////////////////////////////////////////////////
    if (doPlot)
    {
        Plot       p;
        DMatSample effMass(nSample);
        DVec       effMassT, fitErr;
        Index      maxT = (coshModel) ? (nt - 2) : (nt - 1);
        double     e0, e0Err;
        
        p << PlotRange(Axis::x, 0, nt - 1);
        if (!linearModel)
        {
            p << LogScale(Axis::y);
        }
        p << Color("rgb 'blue'") << PlotPredBand(fit.getModel(_), 0, nt - 1);
        p << Color("rgb 'blue'") << PlotFunction(fit.getModel(), 0, nt - 1);
        p << Color("rgb 'red'") << PlotData(data.getData());
        p.display();
        effMass.resizeMat(maxT, 1);
        effMassT.setLinSpaced(maxT, 1, maxT);
        fitErr = fit.variance().cwiseSqrt();
        e0     = fit[central](0);
        e0Err  = fitErr(0);
        if (coshModel)
        {
            FOR_STAT_ARRAY(effMass, s)
            {
                for (Index t = 1; t < nt - 1; ++t)
                {
                    effMass[s](t - 1) = acosh((corr[s](t-1) + corr[s](t+1))
                                              /(2.*corr[s](t)));
                }
            }
        }
        else if (linearModel)
        {
            FOR_STAT_ARRAY(effMass, s)
            {
                for (Index t = 0; t < nt - 1; ++t)
                {
                    effMass[s](t) = corr[s](t) - corr[s](t+1);
                }
            }
        }
        else
        {
            FOR_STAT_ARRAY(effMass, s)
            {
                for (Index t = 1; t < nt; ++t)
                {
                    effMass[s](t - 1) = log(corr[s](t-1)/corr[s](t));
                }
            }
        }
        p.reset();
        p << PlotRange(Axis::x, 1, maxT);
        p << PlotRange(Axis::y, e0 - plotrange*e0Err, e0 + plotrange*e0Err);
        p << Color("rgb 'blue'") << PlotBand(0, maxT, e0 - e0Err, e0 + e0Err);
        p << Color("rgb 'blue'") << PlotHLine(e0);
        p << Color("rgb 'red'") << PlotData(effMassT, effMass);
        p.display();
    }
    if (doHeatmap)
    {
        Plot  p;
        Index n  = data.getFitVarMat().rows();
        DMat  id = DMat::Identity(n, n);
        
        p << PlotMatrix(Math::varToCorr(data.getFitVarMat()));
        p << Caption("correlation matrix");
        p.display();
        if (svdTol > 0.)
        {
            p.reset();
            p << PlotMatrix(id - data.getFitVarMat()*data.getFitVarMatPInv());
            p << Caption("singular space projector");
            p.display();
        }
    }
    
    // output //////////////////////////////////////////////////////////////////
    if (!outFileName.empty())
    {
        Io::save(fit, outFileName);
    }

    return EXIT_SUCCESS;
}
