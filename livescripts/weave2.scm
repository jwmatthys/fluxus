(define col 1)
(define sc 1)
(define rot 45)

(define (draw n)
	(push)
	(colour (vector (* col (gh (+ n 1))) 
					(* col (gh n) )
					(* col (gh (+ n 2)))))
	(scale (vector (* sc (gh (+ n 1))) 
					(* sc (gh n) )
					(* sc (gh (+ n 2)))))
	(scale (vector 4 0.2 0.2))
	(draw_cube)
	(pop)
	(if (eq? n 0)
		1
		(begin
			(push)
			(rotate (vector 0 40 0))
			(translate (vector 2 0 0))
			(draw (- n 1))
			(pop)

			(push)
			(rotate (vector 0 90 0))
			(translate (vector 2 0 0))
			(draw (- n 1))
			(pop)

			(push)
			(rotate (vector 0 270 0))
			(translate (vector 2 0 0))
			(draw (- n 1))
			(pop))))

(define (render)
	(push)
	(draw 5)
	(pop))

(clear)
(clear_colour (vector 0 0 0))
(engine_callback "(render)")

