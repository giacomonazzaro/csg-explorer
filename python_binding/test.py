import pycsg

csg = pycsg.load_csg("../tests/test.csg")
x = pycsg.eval(csg, 0, 0, 0)
print(x)






