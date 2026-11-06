"""Validate both committed contracts and independent core client mock exchanges."""
import json
from pathlib import Path
from jsonschema import Draft202012Validator, FormatChecker

root = Path(__file__).resolve().parents[2]
docs = root / 'docs/modpack-manager'
for name in ('wire.schema.json', 'requests.schema.json', 'core.schema.json'):
    Draft202012Validator.check_schema(json.loads((docs / name).read_text()))
schema = json.loads((docs / 'core.schema.json').read_text())
checks = 3


def check(definition, value):
    global checks
    Draft202012Validator({'$ref': '#/$defs/' + definition, '$defs': schema['$defs']}, format_checker=FormatChecker()).validate(value)
    checks += 1


for line in (docs / 'core.examples.jsonl').read_text().splitlines():
    exchange = json.loads(line)
    method = exchange['request']['method']
    check('request', exchange['request'])
    check(method + '.params', exchange['request']['params'])
    check('response', exchange['response'])
    if 'result' in exchange['response']:
        check(method + '.result', exchange['response']['result'])
    else:
        check('error', exchange['response']['error'])
    invalid = dict(exchange['request']['params'], unexpected=True)
    validator = Draft202012Validator({'$ref': '#/$defs/' + method + '.params', '$defs': schema['$defs']})
    assert not validator.is_valid(invalid), method
    checks += 1
assert not Draft202012Validator({'$ref': '#/$defs/hello.params', '$defs': schema['$defs']}).is_valid(
    {'versions': [], 'client': {'name': 'fixture', 'version': '1'}})
print(f'{checks + 1} contract/schema/mock checks passed')
