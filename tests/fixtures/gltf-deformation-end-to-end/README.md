# Dense deformation pipeline input

These self-contained JSON glTF and hexadecimal GLB representations contain one
three-vertex primitive with directed triangles `(0,1,2)`, `(1,2,0)` and `(0,2,1)`.
Removing duplicate face 1 retains source faces 0 and 2. The opposite directed
triangle remains distinct.

Two users bind the same mesh to different valid skins. Two dense influence sets
retain all eight lanes per vertex, including signed floating zero and nonzero
joint indices in unused lanes. Their floating rows contain respectively zero,
two and five nonzero weights, with sums 0, 1 and 1.125. All-zero rows are admitted
with an advisory; they are neither normalized nor converted to absent rows.
The joint buffer views use an explicit valid four-byte stride.

The installed end-to-end tests use these bytes as real source inputs. Input data
alone does not establish producer, pipeline or platform qualification.
