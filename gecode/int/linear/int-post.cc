/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/*
 *  Main authors:
 *     Christian Schulte <schulte@gecode.org>
 *
 *  Copyright:
 *     Christian Schulte, 2002
 *
 *  Last modified:
 *     $Date$ by $Author$
 *     $Revision$
 *
 *  This file is part of Gecode, the generic constraint
 *  development environment:
 *     http://www.gecode.org
 *
 *  See the file "LICENSE" for information on usage and
 *  redistribution of this file, and for a
 *     DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "gecode/int/rel.hh"
#include "gecode/int/linear.hh"

#include "gecode/support/sort.hh"

#include <climits>
#include <algorithm>

namespace Gecode { namespace Int { namespace Linear {

  bool
  int_precision(Term<IntView> e[], int n, int c) {
    // Decide the required precision
    double sn = 0.0; double sp = 0.0;

    for (int i = n; i--; ) {
      const double l = e[i].a * static_cast<double>(e[i].x.min());
      if (l < 0.0) sn += l; else sp += l;
      const double u = e[i].a * static_cast<double>(e[i].x.max());
      if (u < 0.0) sn += u; else sp += u;
    }
    double cp = (c<0) ? -c : c;
    if ((sn-cp < Limits::Int::double_min) ||
        (sp+cp > Limits::Int::double_max))
      throw NumericalOverflow("Int::linear");

    return ((sn >= Limits::Int::int_min) && (sn <= Limits::Int::int_max) &&
            (sp >= Limits::Int::int_min) && (sp <= Limits::Int::int_max) &&
            (sn-c >= Limits::Int::int_min) && (sn-c <= Limits::Int::int_max) &&
            (sp-c >= Limits::Int::int_min) && (sp-c <= Limits::Int::int_max));
  }

  /*
   * Posting plain propagators
   *
   */

  template <class Val, class View>
  forceinline void
  post_nary(Space* home,
            ViewArray<View>& x, ViewArray<View>& y, IntRelType r, Val c) {
    switch (r) {
    case IRT_LQ:
      if (Lq<Val,View,View >::post(home,x,y,c) == ES_FAILED)
        home->fail();
      break;
    case IRT_EQ:
      if (Eq<Val,View,View >::post(home,x,y,c) == ES_FAILED)
        home->fail();
      break;
    case IRT_NQ:
      if (Nq<Val,View,View >::post(home,x,y,c) == ES_FAILED)
        home->fail();
      break;
    default: GECODE_NEVER;
    }
  }

  void
  post(Space* home, Term<IntView> e[], int n, IntRelType r, int c,
       IntConLevel icl) {
    if ((c < Limits::Int::int_min) || (c > Limits::Int::int_max))
      throw NumericalOverflow("Int::linear");
    normalize<IntView>(e,n,r,c);
    int n_p, n_n;
    bool is_unit = separate<IntView>(e,n,n_p,n_n);

    if (n == 0) {
      switch (r) {
      case IRT_EQ: if (c != 0) home->fail(); break;
      case IRT_NQ: if (c == 0) home->fail(); break;
      case IRT_LQ: if (0 > c)  home->fail(); break;
      default: GECODE_NEVER;
      }
      return;
    }
    if (n == 1) {
      if (n_p == 1) {
        DoubleScaleView y(e[0].a,e[0].x);
        switch (r) {
        case IRT_EQ: GECODE_ME_FAIL(home,y.eq(home,c)); break;
        case IRT_NQ: GECODE_ME_FAIL(home,y.nq(home,c)); break;
        case IRT_LQ: GECODE_ME_FAIL(home,y.lq(home,c)); break;
        default: GECODE_NEVER;
        }
      } else {
        DoubleScaleView y(e[0].a,e[0].x);
        switch (r) {
        case IRT_EQ: GECODE_ME_FAIL(home,y.eq(home,-c)); break;
        case IRT_NQ: GECODE_ME_FAIL(home,y.nq(home,-c)); break;
        case IRT_LQ: GECODE_ME_FAIL(home,y.gq(home,-c)); break;
        default: GECODE_NEVER;
        }
      }
      return;
    }

    bool is_ip = int_precision(e,n,c);
    if (is_unit && is_ip && (icl != ICL_DOM)) {
      if (n == 2) {
        switch (r) {
        case IRT_LQ:
          switch (n_p) {
          case 2:
            if (LqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 1:
            if (LqBin<int,IntView,MinusView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 0:
            if (LqBin<int,MinusView,MinusView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_EQ:
          switch (n_p) {
          case 2:
            if (EqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 1:
            if (EqBin<int,IntView,MinusView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 0:
            if (EqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,-c)
                == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_NQ:
          switch (n_p) {
          case 2:
            if (NqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 1:
            if (NqBin<int,IntView,MinusView>::post(home,e[0].x,e[1].x,c)
                == ES_FAILED) home->fail();
            break;
          case 0:
            if (NqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,-c)
                == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        default: GECODE_NEVER;
        }
      } else if (n == 3) {
        switch (r) {                                                
        case IRT_LQ:
          switch (n_p) {
          case 3:
            if (LqTer<int,IntView,IntView,IntView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 2:
            if (LqTer<int,IntView,IntView,MinusView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 1:
            if (LqTer<int,IntView,MinusView,MinusView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 0:
            if (LqTer<int,MinusView,MinusView,MinusView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_EQ:
          switch (n_p) {
          case 3:
            if (EqTer<int,IntView,IntView,IntView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 2:
            if (EqTer<int,IntView,IntView,MinusView>::post
                (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 1:
            if (EqTer<int,IntView,IntView,MinusView>::post
                (home,e[1].x,e[2].x,e[0].x,-c) == ES_FAILED) home->fail();
            break;
          case 0:
            if (EqTer<int,IntView,IntView,IntView>::post
                (home,e[0].x,e[1].x,e[2].x,-c) == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_NQ:
          switch (n_p) {
          case 3:
            if (NqTer<int,IntView,IntView,IntView>::post
              (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 2:
            if (NqTer<int,IntView,IntView,MinusView>::post
              (home,e[0].x,e[1].x,e[2].x,c) == ES_FAILED) home->fail();
            break;
          case 1:
            if (NqTer<int,IntView,IntView,MinusView>::post
              (home,e[1].x,e[2].x,e[0].x,-c) == ES_FAILED) home->fail();
            break;
          case 0:
            if (NqTer<int,IntView,IntView,IntView>::post
              (home,e[0].x,e[1].x,e[2].x,-c) == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        default: GECODE_NEVER;
        }
      } else {
        ViewArray<IntView> x(home,n_p);
        for (int i = n_p; i--; ) x[i] = e[i].x;
        ViewArray<IntView> y(home,n_n);
        for (int i = n_n; i--; ) y[i] = e[i+n_p].x;
        post_nary<int,IntView>(home,x,y,r,c);
      }
    } else {
      if (is_ip) {
        ViewArray<IntScaleView> x(home,n_p);
        for (int i = n_p; i--; )
          x[i].init(e[i].a,e[i].x);
        ViewArray<IntScaleView> y(home,n_n);
        for (int i = n_n; i--; )
          y[i].init(e[i+n_p].a,e[i+n_p].x);
        if ((icl == ICL_DOM) && (r == IRT_EQ)) {
          if (DomEq<int,IntScaleView>::post(home,x,y,c) == ES_FAILED)
            home->fail();
        } else {
          post_nary<int,IntScaleView>(home,x,y,r,c);
        }
      } else {
        ViewArray<DoubleScaleView> x(home,n_p);
        for (int i = n_p; i--; )
          x[i].init(e[i].a,e[i].x);
        ViewArray<DoubleScaleView> y(home,n_n);
        for (int i = n_n; i--; )
          y[i].init(e[i+n_p].a,e[i+n_p].x);
        if ((icl == ICL_DOM) && (r == IRT_EQ)) {
          if (DomEq<double,DoubleScaleView>::post(home,x,y,c) == ES_FAILED)
            home->fail();
        } else {
          post_nary<double,DoubleScaleView>(home,x,y,r,c);
        }
      }
    }
  }



  /*
   * Posting reified propagators
   *
   */

  template <class Val, class View>
  forceinline void
  post_nary(Space* home,
            ViewArray<View>& x, ViewArray<View>& y, IntRelType r, Val c, BoolView b) {
    switch (r) {
    case IRT_LQ:
      if (ReLq<Val,View,View>::post(home,x,y,c,b) == ES_FAILED)
        home->fail();
      break;
    case IRT_EQ:
      if (ReEq<Val,View,View,BoolView>::post(home,x,y,c,b) == ES_FAILED)
        home->fail();
      break;
    case IRT_NQ:
      {
        NegBoolView n(b);
        if (ReEq<Val,View,View,NegBoolView>::post(home,x,y,c,n) == ES_FAILED)
          home->fail();
      }
      break;
    default: GECODE_NEVER;
    }
  }

  void
  post(Space* home,
       Term<IntView> e[], int n, IntRelType r, int c, BoolView b) {
    if ((c < Limits::Int::int_min) || (c > Limits::Int::int_max))
      throw NumericalOverflow("Int::linear");
    normalize<IntView>(e,n,r,c);
    int n_p, n_n;
    bool is_unit = separate<IntView>(e,n,n_p,n_n);
    if (n == 0) {
      bool fail = false;
      switch (r) {
      case IRT_EQ: fail = (c != 0); break;
      case IRT_NQ: fail = (c == 0); break;
      case IRT_LQ: fail = (0 > c);  break;
      default: GECODE_NEVER;
      }
      if ((fail ? b.zero(home) : b.one(home)) == ME_INT_FAILED)
        home->fail();
      return;
    }
    bool is_ip  = int_precision(e,n,c);
    if (is_unit && is_ip) {
      if (n == 1) {
        switch (r) {
        case IRT_EQ:
          if (n_p == 1) {
            if (Rel::ReEqBndInt<IntView,BoolView>::post(home,e[0].x,c,b)
                == ES_FAILED)
              home->fail();
          } else {
            if (Rel::ReEqBndInt<IntView,BoolView>::post(home,e[0].x,-c,b)
                == ES_FAILED)
              home->fail();
          }
          break;
        case IRT_NQ:
          {
            NegBoolView n(b);
            if (n_p == 1) {
              if (Rel::ReEqBndInt<IntView,NegBoolView>::post(home,e[0].x,c,n)
                  == ES_FAILED)
                home->fail();
            } else {
              if (Rel::ReEqBndInt<IntView,NegBoolView>::post(home,e[0].x,-c,n)
                  == ES_FAILED)
                home->fail();
            }
          }
          break;
        case IRT_LQ:
          if (n_p == 1) {
            if (Rel::ReLqInt<IntView,BoolView>::post(home,e[0].x,c,b)
                == ES_FAILED)
              home->fail();
          } else {
            NegBoolView n(b);
            if (Rel::ReLqInt<IntView,NegBoolView>::post(home,e[0].x,-c-1,n)
                == ES_FAILED)
              home->fail();
          }
          break;
        default: GECODE_NEVER;
        }
      } else if (n == 2) {
        switch (r) {
        case IRT_LQ:
          switch (n_p) {
          case 2:
            if (ReLqBin<int,IntView,IntView>::post(home,e[0].x,e[1].x,c,b)
                == ES_FAILED) home->fail();
            break;
          case 1:
            if (ReLqBin<int,IntView,MinusView>::post(home,e[0].x,e[1].x,c,b)
                == ES_FAILED) home->fail();
            break;
          case 0:
            if (ReLqBin<int,MinusView,MinusView>::post(home,e[0].x,e[1].x,c,b)
                == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_EQ:
          switch (n_p) {
          case 2:
            if (ReEqBin<int,IntView,IntView,BoolView>::post
                (home,e[0].x,e[1].x,c,b)
                == ES_FAILED) home->fail();
            break;
          case 1:
            if (ReEqBin<int,IntView,MinusView,BoolView>::post
                (home,e[0].x,e[1].x,c,b)
                == ES_FAILED) home->fail();
            break;
          case 0:
            if (ReEqBin<int,IntView,IntView,BoolView>::post
                (home,e[0].x,e[1].x,-c,b)
                == ES_FAILED) home->fail();
            break;
          default: GECODE_NEVER;
          }
          break;
        case IRT_NQ:
          {
            NegBoolView n(b);
            switch (n_p) {
            case 2:
              if (ReEqBin<int,IntView,IntView,NegBoolView>::post
                  (home,e[0].x,e[1].x,c,n)
                  == ES_FAILED) home->fail();
              break;
            case 1:
              if (ReEqBin<int,IntView,MinusView,NegBoolView>::post
                  (home,e[0].x,e[1].x,c,b)
                  == ES_FAILED) home->fail();
              break;
            case 0:
              if (ReEqBin<int,IntView,IntView,NegBoolView>::post
                  (home,e[0].x,e[1].x,-c,b)
                  == ES_FAILED) home->fail();
              break;
            default: GECODE_NEVER;
            }
          }
          break;
        default: GECODE_NEVER;
        }
      } else {
        ViewArray<IntView> x(home,n_p);
        for (int i = n_p; i--; ) x[i] = e[i].x;
        ViewArray<IntView> y(home,n_n);
        for (int i = n_n; i--; ) y[i] = e[i+n_p].x;
        post_nary<int,IntView>(home,x,y,r,c,b);
      }
    } else {
      if (is_ip) {
        ViewArray<IntScaleView> x(home,n_p);
        for (int i = n_p; i--; )
          x[i].init(e[i].a,e[i].x);
        ViewArray<IntScaleView> y(home,n_n);
        for (int i = n_n; i--; )
          y[i].init(e[i+n_p].a,e[i+n_p].x);
        post_nary<int,IntScaleView>(home,x,y,r,c,b);
      } else {
        ViewArray<DoubleScaleView> x(home,n_p);
        for (int i = n_p; i--; )
          x[i].init(e[i].a,e[i].x);
        ViewArray<DoubleScaleView> y(home,n_n);
        for (int i = n_n; i--; )
          y[i].init(e[i+n_p].a,e[i+n_p].x);
        post_nary<double,DoubleScaleView>(home,x,y,r,c,b);
      }
    }
  }

}}}

// STATISTICS: int-post

