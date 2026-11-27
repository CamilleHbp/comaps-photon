import json
from pathlib import Path
import unittest
from unittest.mock import patch

import benchmark


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((Path(__file__).resolve().parents[3] /
                                   'data/test_data/valhalla/route.json').read_text())

    def test_shared_request(self):
        points = [[p['lat'], p['lon']] for p in self.fixture['options']['points']]
        self.assertEqual(benchmark.request(points, True), self.fixture['request'])

    def test_demo_url_and_credentials(self):
        self.assertEqual(benchmark.endpoint('https://example.org/api/isochrone'), 'https://example.org/api')
        for value in ['http://example.org', 'https://user:secret@example.org', 'https://example.org?key=x']:
            with self.assertRaises(ValueError):
                benchmark.endpoint(value)

    def test_restriction_metrics(self):
        data = self.fixture['response']
        self.assertTrue(benchmark.metrics(data, True)['accepted'])
        data['trip']['legs'][0]['maneuvers'][0]['toll'] = True
        self.assertFalse(benchmark.metrics(data, True)['accepted'])
        self.assertTrue(benchmark.metrics(data, False)['accepted'])
        data['trip']['warnings'] = [{'code': 401}]
        self.assertFalse(benchmark.metrics(data, False)['accepted'])

    def test_rate_limit_stops_capture(self):
        for status in ('429', '503'):
            with patch('benchmark.subprocess.run') as run:
                run.return_value.stdout = status
                with self.assertRaises(benchmark.ServiceBusy):
                    benchmark.fetch('https://example.org/route', Path('/unused'))


if __name__ == '__main__':
    unittest.main()
