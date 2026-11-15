#include <cassert>
#include <iostream>
#include <vector>
#include "../src/PanelViewportRowIdentity.h"

int main() {
    using namespace ViewReservationUi;
    const std::vector<PanelViewportRowIdentity> rows = {
        {1, 100, false}, {1, 200, true}, {1, 300, true},
        {2, 400, false}, {2, 500, true}, {3, 700, false}
    };
    const auto at = [&](std::size_t i) { return rows[i]; };
    assert(FindPanelViewportRow(rows.size(), {2, 400, false}, 3, at) == 3);
    assert(FindPanelViewportRow(rows.size(), {1, 300, true}, 2, at) == 2);
    assert(FindPanelViewportRow(rows.size(), {1, 999, true}, 2, at) == 2);
    assert(FindPanelViewportRow(rows.size(), {1, 999, true}, 1, at) == 1);
    assert(FindPanelViewportRow(rows.size(), {2, 0, false}, 3, at) == 3);
    assert(FindPanelViewportRow(rows.size(), {4, 0, false}, 3, at) == -1);
    assert(FindPanelViewportRow(rows.size(), {1, 100, true}, 0, at) == 1);
    assert(FindPanelViewportRow(rows.size(), {1, 100, false}, -1, at) == -1);
    assert(FindPanelViewportRow(rows.size(), {0, 100, false}, 0, at) == -1);
    assert(FindPanelViewportRow(0, {1, 100, false}, 0, at) == -1);
    std::cout << "panel viewport row identity tests passed\n";
}
