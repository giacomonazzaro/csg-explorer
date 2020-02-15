import pycsg

filename = "../tests/test.csg"
csg = pycsg.load_csg(filename)
x = pycsg.eval(csg, 0, 0, 0)
pycsg.run_app(filename)






