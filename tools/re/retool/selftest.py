import io
import unittest
from .core import ROOT, WORK, Blocked, digest, functions, read_json, reference
from .cases import corpus
from .native import build, execute
from .verification import first_difference


def selftest(mutation=False, binary=None):
    suite = unittest.defaultTestLoader.discover(str(ROOT / "tools/re/tests"))
    output = io.StringIO()
    result = unittest.TextTestRunner(stream=output, verbosity=1).run(suite)
    value = {"status": "pass" if result.wasSuccessful() and not result.skipped else "mismatch",
             "tests": result.testsRun, "skipped": len(result.skipped), "details": output.getvalue()}
    if mutation:
        f = functions("FloatPack9S")[0]
        path, pe = reference(binary)
        production = ROOT / "src/universal/com_pack.cpp"
        before = digest(production)
        build(mutation=True)
        cases = corpus(f["name"])
        expected = execute(f, cases, original=True, binary=path)
        changed = execute(f, cases, mutation=True)
        mismatch = first_difference(cases, expected, changed)
        if not mismatch or digest(production) != before:
            raise Blocked("Mutation was not detected or production source changed")
        value["mutation"] = {"status": "pass", "detected": mismatch, "production_unchanged": True}
    return value
