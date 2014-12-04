Feature: You can make objects with generic data and edges between objects.
    Background:
        Given I'm Alice
        And I received 1000 XTS from angel
        And I made a wallet otherwallet
        And I switch to wallet default
        And I wait for one block

    Scenario: I can make an object!
        When I make an object obj1 with user data MyUserDataString
        And I wait for one block
        Then Object with ID obj1ID should have user data MyUserDataString
    Scenario: I can make an edge from that object!
        When I make an object obj2 with user data MyUserDataString
        And I make an edge from obj1ID to obj2ID with name Trust and value True
    Scenario: I can make an edge from an account!
    Scenario: I can make an edge from an asset!
    Scenario: Once I transfer an object, only the new owner can update its edges!
