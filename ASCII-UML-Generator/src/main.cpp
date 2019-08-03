#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include "Buffer.h"
#include "ClassNode.h"
#include "Drawing.h"

// Return the x of the rightest parent diagram, and the y of the highest
// parent or member diagram. These are used to draw the next element.
Pos drawDiagram(ClassNode* node, Buffer& buffer)
{
    // We need something to check for cycles and not draw classes twice. So
    // we store the drawned nodes to check if we already drawed them.
    static std::map<std::string, ClassNode*> drawedNodes;

    Pos maxXY(node->getTopRightCorner());
    node->draw(buffer);

    // Members go on the right hand side. We start drawing the first one at the
    // bottom right, then depth first into it keeping track of the highest
    // position reached

    if (!node->parents.empty())
    {
        const int leftMostParentX = [node] {
            const int numParents = node->parents.size();
            if (numParents <= 1)
            {
                return node->pos.x;
            }
            else
            {
                // Let's assume about 20 char for each parents,
                return std::max(
                    0, node->getTopAnchorPoint().x - 20 * (numParents - 1));
            }
        }();

        int cur_parent_x = leftMostParentX;

        for (auto& parent : node->parents)
        {
            // If it's already there no need to recurse
            if (drawedNodes.find(parent->name) == drawedNodes.cend())
            {
                const int arrow_length_y = 10;
                // Because why not make it look prettier
                const int x_padding = 2;
                cur_parent_x += x_padding;

                parent->pos = Pos(cur_parent_x,
                                  node->getTopAnchorPoint().y + arrow_length_y);

                // Before recursing we need to check if we already drawed it
                const Pos parentMaxXY = drawDiagram(parent.get(), buffer);

                maxXY.x = std::max(parentMaxXY.x, maxXY.x);
                maxXY.y = std::max(parentMaxXY.y, maxXY.y);

                cur_parent_x = parentMaxXY.x;
            }

            drawArrow(node->getTopAnchorPoint(),
                      parent->getBottomAnchorPoint(),
                      Relation::Inheritance,
                      buffer);
        }
    }

    if (!node->ownedMembers.empty())
    {
        // Let's estimate the amount of space we need to leave given the total
        // number of members
        const int lowestMemberY = [node] {
            const int numOfMembers =
                node->aggregates.size() + node->ownedMembers.size();
            if (numOfMembers <= 1)
            {
                return node->pos.y;
            }
            else
            {
                // Let's assume each member will need 10 characters total
                // vertical space
                const int approxVerticalSpacePerMember = 10;
                return std::max(node->getBoxHeight(),
                                node->pos.y - approxVerticalSpacePerMember *
                                                  (numOfMembers) / 2);
            }
        }();

        int cur_member_y = lowestMemberY;
        for (auto& member : node->ownedMembers)
        {
            // If it's already there no need to recurse
            if (drawedNodes.find(member->name) == drawedNodes.cend())
            {
                const int arrow_length_x = 45;
                const int parent_member_padding_x = 5; // Enough to make the line curve
                const int cur_member_x =
                    std::max(maxXY.x, arrow_length_x) + parent_member_padding_x;

                member->setLeftAnchorPoint(
                    {cur_member_x, cur_member_y + member->getBoxHeight()});

                const Pos memberMaxXY = drawDiagram(member.get(), buffer);

                maxXY.x = std::max(memberMaxXY.x, maxXY.x);
                maxXY.y = std::max(memberMaxXY.y, maxXY.y);

                cur_member_y = memberMaxXY.y;
            }

            drawArrow(node->getRightAnchorPoint(),
                      member->getLeftAnchorPoint(),
                      Relation::Composition,
                      buffer);
        }
    }
    // TODO: add print aggregates

    // Add to drawed nodes
    drawedNodes.insert(std::make_pair(node->name, node));

    return maxXY;
}

int main()
{
    std::ostream& out = std::cout;

    //     _________           ______________
    //    | MyClass |<>-------| MyOtherClass |
    //     ---------           --------------

    Buffer buffer{};

    //    drawArrow({20, 7}, {20, 17}, Relation::Inheritance, buffer);
    std::shared_ptr<ClassNode> head = std::make_shared<ClassNode>("MyClass");
    head->parents.emplace_back(std::make_shared<ClassNode>("MyParent"));

    head->parents.emplace_back(std::make_shared<ClassNode>("MyOtherParent"));

    head->ownedMembers.emplace_back(std::make_shared<ClassNode>("OtherClass"));
    head->ownedMembers.front()->parents.push_back(
        std::make_shared<ClassNode>("Parent2"));

    head->ownedMembers.front()->parents.push_back(head->parents.front());

    head->ownedMembers.emplace_back(std::make_shared<ClassNode>("OtherClass2"));

    head->pos = {10, 10};
    drawDiagram(head.get(), buffer);

    render(buffer, out);
    // Works on linux
    const char* c = "\u25C6";
    std::cout << "test " << c << " \u22C4-- "
              << "\u25C6-- "
              << "\u25C7-- "
              << "\u25C8-- "
              << " or " << L"\u0444";
    return 0;
}
