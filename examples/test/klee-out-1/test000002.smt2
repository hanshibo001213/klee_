(set-logic QF_AUFBV )
(declare-fun x () (Array (_ BitVec 32) (_ BitVec 8) ) )
(assert (let ( (?B1 (concat  (select  x (_ bv3 32) ) (concat  (select  x (_ bv2 32) ) (concat  (select  x (_ bv1 32) ) (select  x (_ bv0 32) ) ) ) ) ) ) (and  (bvslt  ?B1 (_ bv10 32) ) (=  false (bvslt  (bvadd  (_ bv1 32) ?B1 ) (_ bv10 32) ) ) ) ) )
(check-sat)
(exit)
