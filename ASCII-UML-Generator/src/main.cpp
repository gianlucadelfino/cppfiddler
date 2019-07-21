#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include "Buffer.h"
#include "ClassNode.h"
#include "Drawing.h"

// Return the x of the rightest parent diagram, and the y of the highest
// parent or member diagram. These are used to draw the next element.
Pos drawDiagram(ClassNode* node, Buffer& buffer)
{
    const int arrowLegth = 10;
    Pos maxXY{node->getRightAnchorPoint().x, node->getTopAnchorPoint().y};

    node->draw(buffer);

    // Members go on the right hand side. We start drawing the first one at the
    // bottom right, then depth first into it keeping track of the highest
    // position reached

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
        parent->setBottomAnchorPoint(
            {cur_parent_x, node->getTopAnchorPoint().y + arrowLegth});

        drawArrow(node->getTopAnchorPoint(),
                  parent->getBottomAnchorPoint(),
                  Relation::Inheritance,
                  buffer);
        const Pos parentMaxXY = drawDiagram(parent.get(), buffer);
        maxXY.x = std::max(parentMaxXY.x + parent->getBoxWidth() + 1, maxXY.y);
        maxXY.y = std::max(parentMaxXY.y, maxXY.y);

        cur_parent_x = parentMaxXY.x;
    }

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
            // Let's assume each member will need 10 characters total vertical
            // space
            const int approxVerticalSpacePerMember = 10;
            return std::max(node->getBoxHeight(),
                            node->pos.y - approxVerticalSpacePerMember *
                                              (numOfMembers) / 2);
        }
    }();

    const int members_x =
        std::max(maxXY.x, node->getRightAnchorPoint().x) + arrowLegth;

    int cur_member_y = lowestMemberY;
    for (auto& member : node->ownedMembers)
    {
        member->setLeftAnchorPoint(
            {members_x, cur_member_y + member->getBoxHeight()});

        drawArrow(node->getRightAnchorPoint(),
                  member->getLeftAnchorPoint(),
                  Relation::Composition,
                  buffer);
        const Pos memberMaxXY = drawDiagram(member.get(), buffer);
        maxXY.x = std::max(memberMaxXY.x + member->getBoxWidth() + 1, maxXY.x);
        maxXY.y = std::max(memberMaxXY.y, maxXY.y);

        cur_member_y = memberMaxXY.y;
    }

    // TODO: add print aggregates

    return maxXY;
}

int main()
{
    std::ostream& out = std::cout;

    //     _________           ______________
    //    | MyClass |<>-------| MyOtherClass |
    //     ---------           --------------

    Buffer buffer{};

    std::unique_ptr<ClassNode> head = std::make_unique<ClassNode>("MyClass");
    head->parents.emplace_back(std::make_unique<ClassNode>("MyParent"));
    head->parents.emplace_back(std::make_unique<ClassNode>("MyOtherParent"));

    head->ownedMembers.emplace_back(std::make_unique<ClassNode>("OtherClass"));
    head->ownedMembers.front()->parents.push_back(
        std::make_unique<ClassNode>("Parent2"));

    head->ownedMembers.emplace_back(std::make_unique<ClassNode>("OtherClass2"));

    head->pos = {10, 10};
    drawDiagram(head.get(), buffer);

    render(buffer, out);

    return 0;
}
